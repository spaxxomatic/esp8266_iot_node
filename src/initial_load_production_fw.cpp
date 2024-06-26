#define NO_GLOBAL_HTTPUPDATE
#include <Arduino.h>


#include "debug.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "config.h"
#include "version.h"
#include "eeprom_settings.h"
#include <AsyncMqttClient.h>
#include <ESP8266httpUpdate.h>

#include <Ticker.h>
Ticker blinker;

extern "C" {
#include "user_interface.h"
}

unsigned int count = 0;
void flip() {
  uint8_t state = digitalRead(LED_BUILTIN);
  digitalWrite(LED_BUILTIN, !state);     
}

bool error_signaling_active = false;

#define BLINK_ERROR if (!error_signaling_active) {blinker.detach(); blinker.attach(0.1, flip); error_signaling_active=true;}

#define MAX_JSON_MSG_LEN 255
StaticJsonBuffer<MAX_JSON_MSG_LEN> jsonBuffer;

char str_mac[16] ;

settings_t tmpSettings;
char fw_file[32] ;

void saveSettings() {
  SERIAL_DEBUG("Save config");
  SERIAL_DEBUG(tmpSettings.ssid);
  SERIAL_DEBUG(tmpSettings.password);
  SERIAL_DEBUG(tmpSettings.mqtt_server);  
  if (!EepromConfig.store_conn_info(tmpSettings.ssid, tmpSettings.password, tmpSettings.mqtt_server, tmpSettings.mqtt_username, tmpSettings.mqtt_password))
  {
      SERIAL_DEBUG("Eeprom save failed");
      BLINK_ERROR;
  };
}



#define BLINK_BUSY blinker.detach() ; blinker.attach(0.1, blinkBusy)

static uint8_t blinkCount = 0;
void blinkBusy() { 
  // Blink the LED three times
  if (blinkCount < 6) {    
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    blinkCount++;
  } 
  // Pause for 2 seconds between bursts
  else {    
    digitalWrite(LED_BUILTIN, 0);    
  }
  if (blinkCount == 20)
    blinkCount = 0; // Reset blink count
}

#define CONFIG_SERVER_PORT 9999
bool extract_store_param(JsonObject& json, char* fieldname, char* target_var){
      const char* val = json[fieldname];
      if (strlen(val) > 0){
        strcpy(target_var , val);
      }else{
        SERIAL_DEBUGC("Missing field "); 
        SERIAL_DEBUG(fieldname);  
        BLINK_ERROR;                  
        return false;
      }
      return true;
}          

bool httpRetrieveSettings(){
  HTTPClient http;
  uint8_t retry_cnt = 0;
  bool bret = false;    
  while ( retry_cnt < 5){
    retry_cnt++;
    Serial.print("retrieve cfg from ");
    Serial.println( WiFi.gatewayIP().toString());
    http.begin(WiFi.gatewayIP().toString(), CONFIG_SERVER_PORT, "/config.json");
    // Your Domain name with URL path or IP address with path  
    int httpResponseCode = http.GET();
    
    if (httpResponseCode>0) {
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          String payload = http.getString();
          JsonObject& jsonRoot = jsonBuffer.parseObject(payload); 
          if (!jsonRoot.success()) {
            SERIAL_DEBUG("Json parse fail");    
          }else{    
            bool ret = extract_store_param(jsonRoot, "ssid", tmpSettings.ssid) && 
            extract_store_param(jsonRoot, "pwd", tmpSettings.password) &&
            extract_store_param(jsonRoot, "mqtt_server", tmpSettings.mqtt_server) &&
            extract_store_param(jsonRoot, "mqtt_user", tmpSettings.mqtt_username) &&
            extract_store_param(jsonRoot, "mqtt_pwd", tmpSettings.mqtt_password) &&
            extract_store_param(jsonRoot, "fw_file", fw_file) ;
          }
          bret = true;
    }else {
          Serial.print("HTTP retrieve json: Error code: ");
          Serial.println(httpResponseCode);
    }  
    http.end();
    if (bret) break;
  }
  return bret;
}

#define MSG_UPD_VERSION_NOTFOUND "HTTP_UPDATE_NO_UPDATES"
#define MSG_HTTP_UPDATE_OK "HTTP_UPDATE_OK"

void httpUpdate(bool force){
  Serial.println("Enter httpUpdate");
  BLINK_BUSY;
  if (EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_STARTED){  // a previous run has been ran and a reboot occured 
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
    return;
  }    
  if (force || EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_DO_ON_REBOOT){      
    ESP8266HTTPUpdate ESPhttpUpdate;                
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_STARTED);
    ESPhttpUpdate.rebootOnUpdate(false);
    ESP.wdtDisable();
    //TODO: persist fw_file between boots
    strcpy(fw_file, "/fw.bin");
    Serial1.print("Start httpUpdate - fw ");
    Serial1.println(fw_file);
            
    t_httpUpdate_return ret = ESPhttpUpdate.update(WiFi.gatewayIP().toString(), CONFIG_SERVER_PORT, fw_file, "INIT"); 
    
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAIL Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_FAILED);
        BLINK_ERROR;
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_NO_UPDATE_FOUND);      
        EepromConfig.store_lasterr(MSG_UPD_VERSION_NOTFOUND);
        BLINK_ERROR;
        break;
      case HTTP_UPDATE_OK:
        Serial.println(MSG_HTTP_UPDATE_OK); //afaik we'll never get here, the update reboots after finishing
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
        //EepromConfig.store_lasterr(MSG_HTTP_UPDATE_OK);
        ESP.restart();
        break;
      default:
        Serial.println("Generic failure");
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_FAILED);
        BLINK_ERROR;
    }
  }
}

#define WIFICONFIG_SSID "espserveconfig"
#define WIFICONFIG_PASS "esppassword"

bool connect_config_wifi(){
  WiFi.disconnect();
  delay(500);
  WiFi.begin(WIFICONFIG_SSID, WIFICONFIG_PASS);             // Connect to the network
  Serial.print("Connecting to ");
  Serial.println(WIFICONFIG_SSID); 
  
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++i); Serial.print('.');
    if (i > 20){
      WiFi.disconnect();
      return false;
    }
  }

  Serial.println('\n');
  Serial.println("Conn OK !");  
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer  
  
}

void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(1000);
  Serial.println();
  Serial.println("Boot..");
  pinMode(LED_BUILTIN, OUTPUT);
  // flip the pin every 0.3s
  blinker.attach(0.5, flip);
  //httpUpdate();
  
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(str_mac,"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2], mac[3], mac[4], mac[5]  );
  Serial.println(str_mac);
  SERIAL_DEBUGC("Flash size") ;
  SERIAL_DEBUGC(ESP.getFlashChipRealSize()) ;
  SERIAL_DEBUGC(" Free: ") ;
  SERIAL_DEBUG(ESP.getFreeSketchSpace());
  uint8_t retry = 0;
  while (retry < 3){  
  if (!connect_config_wifi()){
    //try once more
  };
  retry++;
  }
  if (!WiFi.isConnected()){
    BLINK_ERROR;
    delay(1000);
    WiFi.disconnect();
    delay(1000);
    ESP.restart();
    delay(1000);
  }
  
  //if we reached this place, we're connected
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
  httpUpdate(false);

  EepromConfig.begin();

  //load the production config via HTTP 
  ESP.wdtEnable(10000);
  bool bret = httpRetrieveSettings();

  if (bret){
    //load the production firmware
    saveSettings();
    Serial.print("Settings saved, update FW ");
    httpUpdate(true);
    //EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_DO_ON_REBOOT);
    //EEPROM.end();
    //ESP.restart();

  };  
}
void loop(void) {
  if (!error_signaling_active)
  BLINK_ERROR;
}