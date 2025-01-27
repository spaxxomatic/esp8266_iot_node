#define NO_GLOBAL_HTTPUPDATE
//#define DEBUG_ESP_HTTP_CLIENT
// #define DEBUG_ESP_UPDATER
//#define DEBUG_ESP_HTTP_UPDATE
//#define DEBUG_ESP_HTTP_CLIENT
// #define DEBUG_ESP_PORT Serial

#include "debug.h"
#include <Arduino.h>

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
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include <Ticker.h>
Ticker blinker;

#define FORCE_DEEPSLEEP

extern "C" {
#include "user_interface.h"
}
#include "cmd.h"


#define MQTT_ERROR_COUNT_WIFI_RESET 10

uint8_t cnt_mqtt_connectionerror = 0; //counts various errors during operations. If the counter exceeds a certain value, the  wifi will be reset. This is a workaround for the unreliable WiFI.state and wifi event signaling

#define INCR_MQTT_CONN_ERROR cnt_mqtt_connectionerror++

#define RST_MQTT_CONN_ERROR cnt_mqtt_connectionerror = 0

os_timer_t ioHandlerTimer;

bool ioHandlerTick;
bool f_SendConfigReport;

//timer for periodocally reading IO's
void ioTimerCallback(void *pArg) {
      ioHandlerTick = true;
}

IPAddress mqttServerIp;
char* mqttUsername;
char* mqttPassword;
AsyncMqttClient mqttClient;

HTTPClient http;

#ifdef HAS_DHT_SENSOR
DHT dht(DHTPIN, DHTTYPE);
#endif
#ifdef HAS_BUTTON
uint8_t iButtonState = 0;
#endif
#ifdef HAS_MOTION_SENSOR
uint8_t cnt_motion_sensor_countdown=0xFF;
#endif
unsigned int batt;
#define MODE_SENSOR_DISABLED 'D'
#define MODE_SENSOR_ENABLED 1
#define FW_UPLOAD_PORT 9999
unsigned int sensorMode = MODE_SENSOR_ENABLED;
int pwmdir = 0;
double battV;

void httpUpdate();

enum SEM_STATE {
  SEM_BUFF_FREE,
  SEM_BUFF_READING,
  SEM_BUFF_WRITING, 
  SEM_BUFF_AWAITPROCESS
};
volatile uint8_t sem_lock_tempReceivePayloadBuffer = SEM_BUFF_FREE;

enum AUTOCONF_STATE {
  AUTOCONF_DISABLED,
  AUTOCONF_START,
  AUTOCONF_CONNECT_WIFI,  
  AUTOCONF_WIFI_CONNECTED, 
  AUTOCONF_GET_CONFIG,
  AUTOCONF_GET_CONFIG_PROGRESS, 
  AUTOCONF_GET_CONFIG_ERROR,
  AUTOCONF_CONFIG_STORED,
};

volatile uint8_t state_autoconfig_mode = AUTOCONF_DISABLED;

volatile bool f_mqtt_reconnect=false;
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

void connectToWifi();
/*Wifi reconnection handling*/
Ticker timer_wifiReconnect;
#define START_WIFI_CONNCHECK_TIMER if (!timer_wifiReconnect.active()){timer_wifiReconnect.attach(4, connectToWifi);}
#define STOP_WIFI_CONNCHECK_TIMER timer_wifiReconnect.detach()

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

Ticker timer_getConfigConnstr;
#define TRIGGER_GET_CONFIG_CONNSTR if (!timer_getConfigConnstr.active()){ timer_getConfigConnstr.once(2, trigger_get_connstr);}
#define STOP_GET_CONFIG_CONNSTR timer_getConfigConnstr.detach()

void trigger_get_connstr(){
  state_autoconfig_mode = AUTOCONF_GET_CONFIG;
}
/******************* WIFI STATE BUG WORKAROUNDS ************/



void resetWiFi() {
  SERIAL_DEBUG("Wifi reset");
  #if defined(ESP32)
  WiFi.disconnect();
  #else // if defined(ESP32)
  ETS_UART_INTR_DISABLE();
  wifi_station_disconnect();
  ETS_UART_INTR_ENABLE();
  #endif // if defined(ESP32)();
  
  WiFi.mode(WIFI_OFF);  
  WiFi.forceSleepBegin();
  delay(1);
  WiFi.forceSleepWake();
  WiFi.mode(WIFI_STA);  
  delay(1);
  SERIAL_DEBUG("Wifi reset ok");
}

/*******************END WIFI STATE BUG WORKAROUNDS ************/

void onWifiConnect(const WiFiEventStationModeGotIP& event);
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event);

void mqtt_check_conn();


#define BLINK_ERROR if (!blinker.active()) {blinker.attach(0.1, flip);}
#define BLINK_STARTING blinker.attach(0.5, flip);
#define BLINK_STOP blinker.detach();

void flip() {  
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));     // set pin to the opposite state
}

volatile boolean bSendHeartbeat = false;
void timer_heartbeat_handler(){
  bSendHeartbeat = true;
}

void timer_mqttConnCheck_handler(){
if (!mqttClient.connected()){
  //we do not call here reconnect, since we're in a timer routine and it must exit before any yield is called
  f_mqtt_reconnect = true; //trigger reconnection in main thread  
}
}

void enterConfigWifi(){
  // connect to a hardcoded AP and load configuration from a config service
  
  if (state_autoconfig_mode >= AUTOCONF_CONNECT_WIFI) return;
  
  state_autoconfig_mode = AUTOCONF_CONNECT_WIFI;
  
  BLINK_STOP; BLINK_ERROR;    
  START_WIFI_CONNCHECK_TIMER;
  
  Serial.println("Connecting to autoconf wifi");
  
}

void exitConfigWifi(){
  state_autoconfig_mode = AUTOCONF_DISABLED;
}

#define MSG_UPD_VERSION_EXISTS "Upd skip, same fw version"
#define MSG_UPD_VERSION_NOTFOUND "HTTP_UPDATE_NO_UPDATES"
#define MSG_HTTP_UPDATE_OK "HTTP_UPDATE_OK"
void httpUpdate(){  
  if (EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_STARTED){  // a previous run has been ran and a reboot occured 
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
    return;
  }    
  if (EepromConfig.get_http_update_flag() == EEPROOM_HTTP_UPDATE_DO_ON_REBOOT){      
    ESP8266HTTPUpdate ESPhttpUpdate;    
    char* server_ip = EepromConfig.settings.mqtt_server;
    uint16_t update_fw_ver = EepromConfig.settings.update_fw_ver;
    if (update_fw_ver == FW_VERSION){
      SERIAL_DEBUGC(MSG_UPD_VERSION_EXISTS);       
      SERIAL_DEBUG(update_fw_ver);
      EepromConfig.set_http_update_flag(EEPROOM_HTTP_NO_UPDATE_FOUND);
      EepromConfig.store_lasterr(MSG_UPD_VERSION_EXISTS);
      return; 
    }

    //snprintf(tempSendPayloadBuffer, sizeof(tempSendPayloadBuffer),  "http://%s:%i/%d.bin", server_ip, FW_UPLOAD_PORT, update_fw_ver); //misusing the tempSendPayloadBuffer to store the url
    snprintf(tempSendPayloadBuffer, sizeof(tempSendPayloadBuffer),  "%d.bin", update_fw_ver); //misusing the tempSendPayloadBuffer to store the url
    EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_STARTED);
    ESPhttpUpdate.rebootOnUpdate(false);
    ESP.wdtDisable();
    SERIAL_DEBUGC("Start httpUpdate from ");
    SERIAL_DEBUG(tempSendPayloadBuffer);
        
    //t_httpUpdate_return ret = ESPhttpUpdate.update(tempSendPayloadBuffer); //Location of your binary file      
    t_httpUpdate_return ret = ESPhttpUpdate.update(server_ip, FW_UPLOAD_PORT, tempSendPayloadBuffer, String(FW_VERSION)); 
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
        Serial.println(MSG_HTTP_UPDATE_OK);
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_SUCCESS);
        //EepromConfig.store_lasterr(MSG_HTTP_UPDATE_OK);
        esp_bugfree_restart();
        break;
      default:
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_FAILED);
    }
  }
}

char actor_topic[32] ;
char config_topic[32];
char report_topic[32];
char sensor_topic[32];

volatile uint pwmval = 1;

volatile bool sendActorState = false;
Ticker timer_light_on;

void set_actor(){
    pwmval = 1024; 
    digitalWrite(ACTOR_PIN, EepromConfig.settings.actor_state );
    //digitalWrite(SONOFF_LED, !EepromConfig.settings.actor_state );
    sendActorState = true;
}

void actor_off(){
    EepromConfig.settings.actor_state = 0;
    set_actor();
}

void actor_on(bool permanent_on_disable_timer){
    EepromConfig.settings.actor_state = 1;
    set_actor();
    #ifdef HAS_MOTION_SENSOR
    if (! permanent_on_disable_timer)
      timer_light_on.once(float(EepromConfig.settings.motion_sensor_off_timer), actor_off);
    #endif  
}

void actor_toggle(){
    EepromConfig.settings.actor_state = !EepromConfig.settings.actor_state;
    set_actor();
}

Ticker timer_heartbeat;
Ticker timer_mqttConnCheck;

#define ENABLE_MQTT_CHECK_TIMER if (!timer_mqttConnCheck.active()) timer_mqttConnCheck.attach(5, timer_mqttConnCheck_handler)
#define DISABLE_MQTT_CHECK_TIMER timer_mqttConnCheck.detach();

#ifdef HAS_MOTION_SENSOR

void motion_sensor_irq() {
    if (sensorMode != MODE_SENSOR_DISABLED) {
          actor_on(false);
    }
}
#endif

void handleActorMsg(char* payload, int length);
void handleSubscribe();
bool handleConfigMsg();

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  SERIAL_DEBUGC("*RX pub: ");
  SERIAL_DEBUG(topic);
  RST_MQTT_CONN_ERROR;
  if (len>0){
    if (strcmp(topic, config_topic) == 0){      
      //the payload needs to be processed asyncronously since the processing might block too long 
      //so we copy the payload to a buffer and signal the further processing via the sem_lock_tempReceivePayloadBuffer
      if (len >= MAX_JSON_MSG_LEN){
        SERIAL_DEBUG(" !payld > cap");
        return ;
      }

      if (sem_lock_tempReceivePayloadBuffer != SEM_BUFF_FREE){
        SERIAL_DEBUG(" !BuffLocked");  
        return;
      }
      sem_lock_tempReceivePayloadBuffer = SEM_BUFF_WRITING;
      memcpy(tempReceivePayloadBuffer, payload, len) ;
      tempReceivePayloadBuffer[len] = '\0';
      sem_lock_tempReceivePayloadBuffer = SEM_BUFF_AWAITPROCESS;
      //SERIAL_DEBUG(payload);
    }else if (strcmp(topic, actor_topic) == 0){
      handleActorMsg(payload, len);      
    }
  }
}


void onMqttConnect(bool sessionPresent) {
  SERIAL_DEBUG("MQTT connected");
  f_subscribe = true;
  f_SendConfigReport = true;
  RST_MQTT_CONN_ERROR;
  BLINK_STOP;
}


void writeOnlineState(){
    //overwrite the offline status that might have been set by the last will  
  if (mqttClient.publish(sensor_topic, 1, true, "ONLINE") == 0){
    Serial.println("Cannot publish online state");
    INCR_MQTT_CONN_ERROR;
    ENABLE_MQTT_CHECK_TIMER;
  };
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  BLINK_ERROR;
  Serial.print("MQTT disconnect: ");
  INCR_MQTT_CONN_ERROR;
  Serial.print((int) reason);
  Serial.print(" CNT ");
  Serial.println((int) cnt_mqtt_connectionerror);  
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  SERIAL_DEBUG("Sub ACK");
  RST_MQTT_CONN_ERROR;
}

void onMqttUnsubscribe(uint16_t packetId) {
  SERIAL_DEBUG("Unsub ACK");  
}

void onMqttPublish(uint16_t packetId) {
  SERIAL_DEBUG("Pub ACK");
  RST_MQTT_CONN_ERROR;
}

void getStoreConnstring(){
  STOP_GET_CONFIG_CONNSTR;
  if (state_autoconfig_mode == AUTOCONF_GET_CONFIG_PROGRESS){ // a connection is unfinished
    return;
  };

    char connstr_url[64];
    sprintf(connstr_url, "/connstr/%s", str_mac);  

    IPAddress host = WiFi.gatewayIP();
    Serial.print("[HTTP] begin..");
    if (http.begin(host.toString(), 80, connstr_url)) {  // HTTP
      state_autoconfig_mode = AUTOCONF_GET_CONFIG_PROGRESS;
      Serial.print(" GET ");
      Serial.println(connstr_url);
      // start connection and send HTTP header
      int httpCode = http.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf(" code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        // get lenght of document (is -1 when Server sends no Content-Length header)
          int len = http.getSize();

          // create buffer for read
          uint8_t buff[128] = { 0 };

          // get tcp stream
          WiFiClient * stream = &http.getStream();

            // read all data from server
            while (http.connected() && (len > 0 || len == -1)) {
              // get available data size
              size_t size = stream->available();

              if (size) {
                // read up to 128 byte
                int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                // write it to Serial
                if (len > 0) {len -= c;}
              }
              delay(1);
            }
            
          int iret = command_set_conn_params((char*) buff, stream);
          if (iret == 0){
            state_autoconfig_mode = AUTOCONF_CONFIG_STORED;
          //confirm
            http.PUT("OK"); 
          }else{
            http.PUT("Settings store error"); 
          }
        }else{
          state_autoconfig_mode = AUTOCONF_GET_CONFIG_ERROR;
      //send confirmation
      
            int httpCode = http.PUT("ERR");
        }
        
      } else {
        Serial.printf(" error: %s\n", http.errorToString(httpCode).c_str());
        state_autoconfig_mode = AUTOCONF_GET_CONFIG_ERROR;
      }      
      //http.end();
      yield();      
      
      http.end();

    } else {
      Serial.printf("[HTTP] Unable to connect\n");
      state_autoconfig_mode = AUTOCONF_GET_CONFIG_ERROR;
    }    

}

void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(1000);
  Serial.println();
  Serial.println("Boot..");
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_FLASH, OUTPUT);
  // flip the pin every 0.3s
  BLINK_STARTING;
  
  #ifdef HAS_MOTION_SENSOR
  attachInterrupt(digitalPinToInterrupt(MOTION_SENSOR_PIN), motion_sensor_irq, FALLING);
  #endif
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(str_mac,"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2], mac[3], mac[4], mac[5]  );
  sprintf(actor_topic, "/actor/%s", str_mac);
  sprintf(config_topic, "/config/%s", str_mac);
  
  sprintf(sensor_topic, "/sensor/%s", str_mac);
  sprintf(report_topic, "/report/%s", str_mac);
  Serial.println(str_mac);

  SERIAL_DEBUGC("Flash size") ;
  SERIAL_DEBUGC(ESP.getFlashChipRealSize()) ;
  SERIAL_DEBUGC(" Free: ") ;
  SERIAL_DEBUG(ESP.getFreeSketchSpace());
  //set up timer
  os_timer_setfn(&ioHandlerTimer, ioTimerCallback, NULL);
  os_timer_arm(&ioHandlerTimer, OS_MAIN_TIMER_MS, true);
  
  EepromConfig.begin();
    
  http.setReuse(true);
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  
  if (EepromConfig.readEepromParams()){ //if we have saved params, try to connect the wlan
 
  Serial.print("MQTT srv parms");
  Serial.print(EepromConfig.settings.mqtt_server);
  Serial.print(EepromConfig.settings.mqtt_username);
  Serial.print(EepromConfig.settings.mqtt_password);
  
  WiFi.hostByName(EepromConfig.settings.mqtt_server, mqttServerIp) ;
  WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.setServer(mqttServerIp, 1883);  
  mqttClient.setCredentials(EepromConfig.settings.mqtt_username, EepromConfig.settings.mqtt_password);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setKeepAlive(10).setCleanSession(true).setClientId(str_mac)
    .setWill(sensor_topic, 0, true, "OFFLINE");;
         
  } else {
    state_autoconfig_mode = AUTOCONF_START ;
  }

  ESP.wdtEnable(5000);
  pinMode(ACTOR_PIN, OUTPUT);
  #ifdef HAS_BUTTON
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
  
  START_WIFI_CONNCHECK_TIMER;
  BLINK_STOP;  
  //enable PWM on led pin
  analogWriteFreq(100); // Hz freq
  timer_heartbeat.attach(EepromConfig.settings.log_freq, timer_heartbeat_handler);
  
 }

volatile bool f_wifi_conn_in_progress = false;
int cnt_wlan_connectionerror = 0;
#define INCR_WLAN_CONN_ERR_AND_RETURN cnt_wlan_connectionerror++; f_wifi_conn_in_progress = false; return ;
#define RST_WLAN_CONN_ERR cnt_wlan_connectionerror = 0;


void connectToWifi(){
  
  if (f_wifi_conn_in_progress) {
    Serial.print ("Wifi conn in progress");
    return;  
  }

  if (WiFi.status() == WL_CONNECTED){
      Serial.print ("Already connected");
      return;
  }

  char* ssid = EepromConfig.settings.ssid;
  char* passwd =  EepromConfig.settings.password;
  
  Serial.println ("Ck Wi-Fi ");

  f_wifi_conn_in_progress = true;

  if (state_autoconfig_mode >= AUTOCONF_START){    
    ssid = WIFI_AP_CONFIG;
    passwd = WIFI_AP_CONFIG_PW;
  }  
  
  Serial.print (" Wi-Fi ");
  Serial.print (ssid);
  Serial.println (passwd);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  
  WiFi.setAutoReconnect(false);  
  wl_status_t status = WiFi.begin(ssid, passwd) ;
  if (status == WL_CONNECT_FAILED){
    Serial.println ("Conn failed");
    INCR_WLAN_CONN_ERR_AND_RETURN;
  };
  if (status == STATION_CONNECT_FAIL){
    Serial.println ("Conn fail");
    INCR_WLAN_CONN_ERR_AND_RETURN;
  }; 
  if (status == STATION_IDLE){
    Serial.println ("err idle");
    INCR_WLAN_CONN_ERR_AND_RETURN;
  };    
  if (WiFi.status() == WL_DISCONNECTED){
    //WL_DISCONNECTED means usually that credentials are not valid
    Serial.println ("CW disconnected");
    INCR_WLAN_CONN_ERR_AND_RETURN;
  }

  f_wifi_conn_in_progress = false;

}

void onWifiConnect(const WiFiEventStationModeGotIP& event)
{
  STOP_WIFI_CONNCHECK_TIMER;
  #ifdef DEBUG
    Serial.println("Connected to Wi-Fi.");
    Serial.println(WiFi.localIP());
  #endif
  #ifdef DEBUG
    Serial.println("Connecting to MQTT...");
  #endif    
  WiFi.onStationModeDisconnected(onWifiDisconnect);
  //only make conn persistent if we're not in config mode
  if (state_autoconfig_mode == AUTOCONF_DISABLED){      
    //if we reached this place, we're connected
    ENABLE_MQTT_CHECK_TIMER;
    Serial.print("Connected. IP: ");
    WiFi.persistent(true);
  }else{
    Serial.print("Autoconf wifi connected ");
    state_autoconfig_mode = AUTOCONF_WIFI_CONNECTED;
  }
  Serial.println(WiFi.localIP());  
}

// ----------------------------------------------------------------------

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event)
{
  BLINK_ERROR;    
  Serial.print("Lost Wi-Fi ");
  Serial.print(event.ssid);
  Serial.print(" ");
  Serial.println (event.reason);
  
  DISABLE_MQTT_CHECK_TIMER;
  
  if (WiFi.status() == WL_CONNECTED) { // handles wrong state report
    Serial.println("Status is wrong, disconnect");
    resetWiFi();
  } 
  
  START_WIFI_CONNCHECK_TIMER;
}

const char jsonFormatBattOnly[] = "{\"batt\":%s}";
const char jsonFormatErr[] = "{\"error\":\"%s\"}";
const char jsonFormatReport[] = "{\"fw\":\"%d\",\"location\":\"%s\",\"cap\":\"%s\", \"lasterr\":\"%s\"}";

float batt_voltage=0.00f;
char tbuff[8];
char hbuff[8];
char bvbuff[8];

void sendHeartbeat(){
  //batt_voltage = ESP.getVcc()/(float)1000;
  //snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/data", sensor_topic);
  //sprintf(tempSendPayloadBuffer,jsonFormatBattOnly, dtostrf(batt_voltage, 6, 2, bvbuff));
  //if (mqttClient.publish(tempTopicBuffer, 1, true, tempSendPayloadBuffer) == 0){
  //   SERIAL_DEBUGC("\n MQTT err send.") ;
  //};
  SERIAL_DEBUG("Sending HB") ;  
  writeOnlineState();
}

void mqtt_check_conn(){ //retry reconnect should be called periodically by the chk_connect timer 
    SERIAL_DEBUG("MQTT CKCON") ;   
    if (!mqttClient.connected()){    
        mqttClient.disconnect(true);
        SERIAL_DEBUG("MQTT reconnect ..") ;
        yield();  
        mqttClient.connect();
    }
}

void loop_serial(){
  check_serial_cmd(); //serial command avail?
  yield();
}

void loop_autoconf_state(){
  switch (state_autoconfig_mode){
    case AUTOCONF_START:
    case  AUTOCONF_CONNECT_WIFI :
      enterConfigWifi(); break;
    case AUTOCONF_WIFI_CONNECTED:
      state_autoconfig_mode = AUTOCONF_GET_CONFIG;
    case AUTOCONF_GET_CONFIG:
      getStoreConnstring(); break;
    case AUTOCONF_GET_CONFIG_PROGRESS: break;
      //just wait 
    case AUTOCONF_GET_CONFIG_ERROR: 
      // if there was an error, retrigger it
      TRIGGER_GET_CONFIG_CONNSTR; break;      
    case AUTOCONF_CONFIG_STORED:       
      esp_bugfree_restart(); break;
      
    default: break;
  } 
  yield();
}

void loop_connstack(){
    /* ----------- Communication handlinng -------------*/
  if (f_mqtt_reconnect){ 
      f_mqtt_reconnect = false;
      mqtt_check_conn();
    }
  yield();
}

void loop_mqtt(){
  if (sendActorState){
    sendActorState = false;
    snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/state", actor_topic);
    itoa(EepromConfig.settings.actor_state,tempSendPayloadBuffer,10);
    if (mqttClient.publish(tempTopicBuffer, 1, true, tempSendPayloadBuffer) == 0){
      SERIAL_DEBUG("MQTT pub failed") ;
      BLINK_ERROR;      
    }else{
      BLINK_STOP;
    };  
    yield();
  }
  if (bSendHeartbeat){
      bSendHeartbeat = false;
      sendHeartbeat();
      yield();
  }  
  if (f_subscribe){
      f_subscribe = false;
      handleSubscribe();
      yield();
  }  
  if (sem_lock_tempReceivePayloadBuffer == SEM_BUFF_AWAITPROCESS){    
      //we received a config message, process it async    
      handleConfigMsg();
      sem_lock_tempReceivePayloadBuffer = SEM_BUFF_FREE;      
      yield();
  }    
  if (f_deleteRemoteConfig){
      f_deleteRemoteConfig = false;
      mqttClient.publish(config_topic, 1, false);
      yield();
  }  
  if (f_SendConfigReport){
      f_SendConfigReport = false;  
      char lasterr[EEPARAM_LASTERR_LEN] ;
      EepromConfig.get_lasterr(lasterr);
      lasterr[EEPARAM_LASTERR_LEN-1] = '\0'; //buffer overflow protection
      sprintf(tempSendPayloadBuffer,jsonFormatReport, FW_VERSION, EepromConfig.settings.location, CAPABILITIES, lasterr);
      mqttClient.publish(report_topic, 1, true, tempSendPayloadBuffer);
      yield();
  }

}

void loop_hw_io(){
  //inputs handling
  if (ioHandlerTick){
    ioHandlerTick = false;
    //we read the button state only once every tick, (debouncing)
    #ifdef HAS_BUTTON
    uint8_t val = digitalRead(BUTTON_PIN);  // read input value
     if (val == HIGH) {  //check if the input is HIGH (button released)
       iButtonState = 0;
     } else {
       iButtonState += 1; //increment, but only the value 1 will trigger an action
     }
     if (iButtonState == 1){
       //only the first press after a depress shall trigger a state change
       actor_toggle();
     }
     if (iButtonState >= 20){ // around 20*0.2 seconds 
       //4 presses - enter wifi manager
       Serial.println("BP->Enter config");
       if (state_autoconfig_mode == AUTOCONF_DISABLED || state_autoconfig_mode == AUTOCONF_GET_CONFIG_ERROR){
        command_enter_config(nullptr);
       }
     }     
    #endif
    
    if (!blinker.active()){
      /* show normal operation by dimming the LED up and down */  
      pwmval+=pwmdir;
      analogWrite(LED_BUILTIN, pwmval);
      if (pwmval >=980) pwmdir = -50;
      if (pwmval <= 40) pwmdir = 50;
    }    
  }
}


void handleSubscribe(){  
  writeOnlineState();
  mqttClient.subscribe(actor_topic, 2);  
  mqttClient.subscribe(config_topic, 2);    
}

bool handleConfigMsg(){
    if (sem_lock_tempReceivePayloadBuffer == SEM_BUFF_WRITING){
      SERIAL_DEBUG ("! bufflock");  
      SERIAL_DEBUG (tempReceivePayloadBuffer);
      return false;
    }
    sem_lock_tempReceivePayloadBuffer = SEM_BUFF_READING;
    SERIAL_DEBUG ("HCM");
    SERIAL_DEBUG (tempReceivePayloadBuffer);
    JsonObject& jsonRoot = jsonBuffer.parseObject(tempReceivePayloadBuffer); 
    // Test if parsing succeeds.
    if (!jsonRoot.success()) {
        SERIAL_DEBUG("Payld parse fail");        
    }else{    
        int deepsleep = jsonRoot["deepsleep"];
        if (deepsleep > 0){
            EepromConfig.store_deepsleep(deepsleep);
            SERIAL_DEBUGC ("Deepsleep value set to ");
            SERIAL_DEBUG (deepsleep);
        }
        const char*  location = jsonRoot["location"];
        //root.prettyPrintTo(Serial);
        if (location){
          SERIAL_DEBUG ("Set location");
          EepromConfig.store_location(location);
          //delete config and delete it
        }
        int log_freq = jsonRoot["log_freq"];
        if (log_freq){
          SERIAL_DEBUGC ("Set log_freq");
          SERIAL_DEBUG (log_freq);
          if (log_freq > 0){
            EepromConfig.store_log_freq(log_freq);
            timer_heartbeat.detach();
            timer_heartbeat.attach(EepromConfig.settings.log_freq, timer_heartbeat_handler);
          }else{
            SERIAL_DEBUG ("No freq value");
          }
        }
        int report = jsonRoot["report"];
        if (report){
          f_SendConfigReport = true;
        }  
        uint16_t update_firmware_version = jsonRoot["update"];
        if (update_firmware_version){
          SERIAL_DEBUGC ("Set update FW ");
          command_trigger_update(update_firmware_version);
        }                
        
        #ifdef HAS_MOTION_SENSOR
        int val = jsonRoot["motion_sensor_timer"];
        if (val){
          SERIAL_DEBUGC ("Set motion_sensor_timer: ");
          SERIAL_DEBUG (val);
          if (val >= MOTION_SENSOR_DEFAULT_TIMER){
            EepromConfig.storeMotionSensorOffTimer(val);
          }else{
            SERIAL_DEBUG ("Invalid mot sensor timer val");
          }
        }
        #endif
        //f_deleteRemoteConfig = true;
    }    
    jsonBuffer.clear();    
    return true;    
}

void handleActorMsg(char* payload, int length){
  SERIAL_DEBUG ("H_AM");  
  if (length > 0){
    if (payload[0] == '0'){
      actor_off();
      sensorMode = MODE_SENSOR_ENABLED;
    }else{
      if (payload[0] == MODE_SENSOR_DISABLED) { //this is a special value for "permanent on"
        sensorMode = MODE_SENSOR_DISABLED;
        SERIAL_DEBUG ("sensor dis");
        actor_on(true);
      }else{
        SERIAL_DEBUG ("sensor en");
        sensorMode = MODE_SENSOR_ENABLED;
        actor_on(false);
      }
    }
  }

}

void loop(void) {
  yield();
  //delay(50);
  loop_hw_io();
  loop_serial();
  if (state_autoconfig_mode > AUTOCONF_DISABLED){
    //we are in autoconf mode, handle only the autoconf loop
    loop_autoconf_state();
  }else{
    loop_mqtt();
    loop_connstack();  
  }
}

void command_reset	(char* params){
		Serial.println("Reset");
    mqttClient.disconnect(true);
    delay(400);
		WiFi.disconnect();
    delay(400);
    esp_bugfree_restart();
    delay(100);
}

void command_enter_config	(char* params){
		Serial.println("Enter conf");
    mqttClient.disconnect(true);
		WiFi.disconnect();
    state_autoconfig_mode = AUTOCONF_START;
}