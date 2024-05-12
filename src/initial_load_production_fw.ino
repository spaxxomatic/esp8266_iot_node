#define NO_GLOBAL_HTTPUPDATE
//#define DEBUG_ESP_HTTP_CLIENT
//#define DEBUG_ESP_UPDATER
//#define DEBUG_ESP_HTTP_UPDATE
//#define DEBUG_ESP_HTTP_CLIENT

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
#include <esp8266httpupdate.h>

#include <Ticker.h>
Ticker blinker;

extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;

bool tickOccured;

// start of timerCallback
void timerCallback(void *pArg) {
      tickOccured = true;
}


#ifdef HAS_BUTTON
uint8_t iButtonState = 0;
#endif
#ifdef HAS_MOTION_SENSOR
uint8_t cnt_motion_sensor_countdown=0xFF;
#endif
unsigned int batt;
#define MODE_SENSOR_DISABLED 'D'
#define MODE_SENSOR_ENABLED 1

unsigned int sensorMode = MODE_SENSOR_ENABLED;
int pwmdir = 0;
double battV;

enum SEM_STATE {
  SEM_BUFF_FREE,
  SEM_BUFF_READING,
  SEM_BUFF_WRITING, 
  SEM_BUFF_AWAITPROCESS
};

volatile uint8_t sem_lock_tempReceivePayloadBuffer = SEM_BUFF_FREE;
volatile bool f_reconnect=false;
volatile bool f_subscribe=false;
volatile bool f_processConfig=false;
volatile bool f_deleteRemoteConfig=false;
volatile bool f_handleActorEvent=false;

#define MAX_JSON_MSG_LEN 255
StaticJsonBuffer<MAX_JSON_MSG_LEN> jsonBuffer;

char tempSendPayloadBuffer[256];
char tempReceivePayloadBuffer[MAX_JSON_MSG_LEN + 1];
char tempTopicBuffer[128];

ADC_MODE(ADC_VCC);
char str_mac[16] ;

settings_t tmpSettings;
char fw_file[32] ;

void saveConfigParams() {
  SERIAL_DEBUG("Save config");
  SERIAL_DEBUG(tmpSettings.ssid);
  SERIAL_DEBUG(tmpSettings.password);
  SERIAL_DEBUG(tmpSettings.mqtt_server);  
  if (!EepromConfig.store_conn_info(tmpSettings.ssid, tmpSettings.password, tmpSettings.mqtt_server, tmpSettings.mqtt_username, tmpSettings.password))
  {
      SERIAL_DEBUG("Eeprom save failed");
  };
}

unsigned int count = 0;
void flip() {
  int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state

  /*
  ++count;
  // when the counter reaches a certain value, start blinking like crazy
  if (count == 20) {
    blinker.attach(0.1, flip);
  }
  // when the counter reaches yet another value, stop blinking
  else if (count == 120) {
    blinker.detach();
  }
  */
}

volatile boolean bSendHeartbeat = false;
void timer_heartbeat_handler(){
  bSendHeartbeat = true;
}

#define CONFIG_SERVER_PORT 9999
bool extract_store_param(JsonObject& json, char* fieldname, char* target_var){
          const char* val = json[fieldname];
          if (strlen(val) > 0){
            strcpy(target_var , val);
          }else{
            SERIAL_DEBUGC("Missing field "); 
            SERIAL_DEBUG(fieldname);                    
          }
}          

bool httpRetrieveSettings(){
  HTTPClient http;
  
  http.begin(WiFi.gatewayIP().toString(), CONFIG_SERVER_PORT, "/config.json");
  // Your Domain name with URL path or IP address with path  
  int httpResponseCode = http.GET();
      
  if (httpResponseCode>0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        String payload = http.getString();
        Serial.println(payload);
        JsonObject& jsonRoot = jsonBuffer.parseObject(payload); 
        if (!jsonRoot.success()) {
          SERIAL_DEBUG("Json parse fail");        
          return false;
        }else{    
          bool ret = extract_store_param(jsonRoot, "ssid", tmpSettings.ssid) && 
          extract_store_param(jsonRoot, "pwd", tmpSettings.password) &&
          extract_store_param(jsonRoot, "mqtt_server", tmpSettings.password) &&
          extract_store_param(jsonRoot, "mqtt_user", tmpSettings.mqtt_username) &&
          extract_store_param(jsonRoot, "mqtt_pwd", tmpSettings.mqtt_password) &&
          extract_store_param(jsonRoot, "fw_file", fw_file) ;
          return ret;
        }
        return true;
  }else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
        return false;
  }  
}

#define MSG_UPD_VERSION_NOTFOUND "HTTP_UPDATE_NO_UPDATES"
#define MSG_HTTP_UPDATE_OK "HTTP_UPDATE_OK"
void httpUpdate(){
  if (EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_STARTED){  // a previous run has been ran and a reboot occured 
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
    return;
  }    
  if (EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_DO_ON_REBOOT){      
    ESP8266HTTPUpdate ESPhttpUpdate;                
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_STARTED);
    ESPhttpUpdate.rebootOnUpdate(false);
    ESP.wdtDisable();
    SERIAL_DEBUGC("Start httpUpdate - fw ");
    SERIAL_DEBUG(fw_file);
            
    t_httpUpdate_return ret = ESPhttpUpdate.update(WiFi.gatewayIP().toString(), CONFIG_SERVER_PORT, fw_file, "INIT"); 
    //t_httpUpdate_return ret = ESPhttpUpdate.update(tempSendPayloadBuffer, FW_VERSION); 
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAIL Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_FAILED);
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_NO_UPDATE_FOUND);      
        EepromConfig.store_lasterr(MSG_UPD_VERSION_NOTFOUND);
        break;
      case HTTP_UPDATE_OK:
        Serial.println(MSG_HTTP_UPDATE_OK); //afaik we'll never get here, the update reboots after finishing
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
        //EepromConfig.store_lasterr(MSG_HTTP_UPDATE_OK);
        ESP.restart();
        break;
      default:
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_FAILED);
    }
  }
}


volatile uint pwmval = 1;


Ticker timer_heartbeat;

#define WIFICONFIG_SSID "espserveconfig"
#define WIFICONFIG_PASS "esppassword"

void connect_config_wifi(){
  WiFi.begin(WIFICONFIG_SSID, WIFICONFIG_PASS);             // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(WIFICONFIG_SSID); 
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++i); Serial.print('.');
  }

  Serial.println('\n');
  Serial.println("Conn OK !");  
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer  
  
}
/*
AsyncMqttClient mqttClient;

void connect_mqtt(){
  
  mqttClient.setServer(mqttServerIp, 1883);    
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setKeepAlive(10).setCleanSession(true).setClientId(str_mac);
  mqttClient.connect();
}
*/
void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(1000);
  Serial.println();
  Serial.println("Boot..");
  pinMode(LED_BUILTIN, OUTPUT);
  // flip the pin every 0.3s
  blinker.attach(0.3, flip);
  //httpUpdate();
  
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(str_mac,"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2], mac[3], mac[4], mac[5]  );
  Serial.println(str_mac);
  SERIAL_DEBUGC("Free space") ;
  SERIAL_DEBUG(ESP.getFreeSketchSpace());
  SERIAL_DEBUGC("Flash size") ;
  SERIAL_DEBUG(ESP.getFlashChipRealSize()) ;
  
  //set up timer
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, OS_MAIN_TIMER_MS, true);
  
  EepromConfig.begin();
  int i = 0;

  connect_config_wifi();

  blinker.detach();
  //if we reached this place, we're connected
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  //connect to MQTT Server and load the production config
  ESP.wdtEnable(5000);
  
  if (httpRetrieveSettings()){
    //load the production firmware
    httpUpdate();
  };
  
  //WiFi.hostByName(EepromConfig.settings.mqtt_server, mqttServerIp) ;
  //if (mqttServerIp == IPAddress(0,0,0,0)){ //host name or ip adress is wrong, enter config
  //  Serial.println("Mqtt server config error, enter config");
  
}

void loop(void) {
  yield();  
}