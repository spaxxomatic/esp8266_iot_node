const int FW_VERSION = 1244;
#include "debug.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "config.h"
#include "eeprom_settings.h"
#include <AsyncMqttClient.h>
#include <WiFiManager.h>
#include<esp8266httpupdate.h>
//#include <ArduinoOTA.h>
#ifdef HAS_DHT_SENSOR
#include "DHT.h"
#endif

#include <Ticker.h>
Ticker blinker;

#ifdef HAS_DHT_SENSOR
#define DHTTYPE DHT11
#define DHTPIN 13 //GPIO13
#endif

#define FORCE_DEEPSLEEP

extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;
os_timer_t mqttReconnectTimer;
bool tickOccured;
bool f_SendConfigReport;
// start of timerCallback
void timerCallback(void *pArg) {
      tickOccured = true;
}

IPAddress mqttServerIp;
AsyncMqttClient mqttClient;

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


ADC_MODE(ADC_VCC);
WiFiManager wifiManager;
char str_mac[16] ;

void saveConfigParams() {
  SERIAL_DEBUG(" cb: save config");
  SERIAL_DEBUG(wifiManager.getValidSsid());
  SERIAL_DEBUG(wifiManager.getValidPwd());
  SERIAL_DEBUG(wifiManager.getMqttAddress());
  //EepromConfig.store_conn_info(WiFi.SSID().c_str(), WiFi.psk().c_str());
  if (!EepromConfig.store_conn_info(wifiManager.getValidSsid(),
    wifiManager.getValidPwd(), wifiManager.getMqttAddress()))
    {
      SERIAL_DEBUG("Eeprom save failed");
    };
}

unsigned int count = 0;
void flip() {
  int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state

  ++count;
  // when the counter reaches a certain value, start blinking like crazy
  if (count == 20) {
    blinker.attach(0.1, flip);
  }
  // when the counter reaches yet another value, stop blinking
  else if (count == 120) {
    blinker.detach();
  }
}

volatile boolean bSendHeartbeat = false;
void timer_heartbeat_handler(){
  bSendHeartbeat = true;
}

void timer_mqttConnCheck_handler(){
if (!mqttClient.connected()){
  f_reconnect = true;
}
}

void enterWifiManager(){
  //try first default password on the ssid with the best signal

  #ifdef DEBUG_ON
    wifiManager.setDebugOutput(true);
  #endif
  wifiManager.setConfigPortalTimeout(300); //wait 5 min in config mode
  wifiManager.setConnectTimeout(20); //wait max 20 sec for wlan connect
  wifiManager.setSaveConfigCallback(*saveConfigParams);
  if (!wifiManager.startConfigPortal("ESP_WAITING_CONFIG", "pass")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(1000);
  }
}
/*
void httpUpdate(){  
  t_httpUpdate_return ret = ESPhttpUpdate.update("http://192.168.1.11/file.bin"); //Location of your binary file
  //t_httpUpdate_return  ret = ESPhttpUpdate.update("https://server/file.bin");
  
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
  }
}
*/
char actor_topic[32] ;
char config_topic[32];
char report_topic[32];
char sensor_topic[32];

volatile uint pwmval = 1;

volatile bool sendActorState = false;
Ticker timer_light_on;

void set_actor(){
    pwmval = 1024; //
    digitalWrite(ACTOR_PIN, EepromConfig.settings.actor_state );
    //digitalWrite(SONOFF_LED, !EepromConfig.settings.actor_state );
    sendActorState = true;
}
void actor_on(bool permanent_on_disable_timer){
    EepromConfig.settings.actor_state = 1;
    set_actor();
    #ifdef HAS_MOTION_SENSOR
    if (! permanent_on_disable_timer)
      timer_light_on.once(EepromConfig.motion_sensor_off_timer, actor_off);
    #endif  
}

void actor_off(){
    EepromConfig.settings.actor_state = 0;
    set_actor();
}

void actor_toggle(){
    EepromConfig.settings.actor_state = !EepromConfig.settings.actor_state;
    set_actor();
}

Ticker timer_heartbeat;
Ticker timer_mqttConnCheck;

#ifdef HAS_MOTION_SENSOR

void motion_sensor_irq() {
    if (sensorMode != MODE_SENSOR_DISABLED) {
          actor_on(false);
    }
}
#endif

void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(1000);
  Serial.println();
  Serial.println("Boot..");
  pinMode(LED_BUILTIN, OUTPUT);
  // flip the pin every 0.3s
  blinker.attach(0.3, flip);
  //httpUpdate();
  
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
  //set up timer
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, OS_MAIN_TIMER_MS, true);
  
  EepromConfig.begin();
  int i = 0;
  
  if (EepromConfig.readEepromParams()){ //if we have saved params, try to connect the wlan
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("WiFi login:");
      Serial.println(EepromConfig.settings.ssid);
      Serial.println(EepromConfig.settings.password);
      WiFi.begin(EepromConfig.settings.ssid, EepromConfig.settings.password);
      delay(100);
      i++;
      if (i > 4) {
              Serial.println("WiFi fail, enter WiFiManager ..");
              enterWifiManager();
            };
    }
  }else{
    //no valid  eprom config, try to connect with default values
    //EepromConfig.getDefaultConfig();
    //go in config mode
    enterWifiManager();
  }
  //if we reached this place, we're connected
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());
  
  ESP.wdtEnable(5000);
  pinMode(ACTOR_PIN, OUTPUT);
  #ifdef HAS_BUTTON
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
  //digitalWrite(SONOFF_LED, 1);
  //pinMode(SONOFF_LED, INPUT_PULLUP);
  #ifdef HAS_DHT_SENSOR
  dht.begin();
  #endif  
  //Serial.print("Connect MQTT srv ");
  //Serial.println(EepromConfig.settings.mqtt_server);
  WiFi.hostByName(EepromConfig.settings.mqtt_server, mqttServerIp) ;
  if (mqttServerIp == IPAddress(0,0,0,0)){ //host name or ip adress is wrong, enter config
    Serial.println("Mqtt server config error, enter config");
    enterWifiManager();
  }
  Serial.println(mqttServerIp);
  mqttClient.setServer(mqttServerIp, 1883);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setKeepAlive(10).setCleanSession(true).setClientId(str_mac);
  mqttClient.connect();
  //enable PWM on led pin
  blinker.detach();
  analogWriteFreq(100); // Hz freq
  timer_heartbeat.attach(EepromConfig.settings.log_freq, timer_heartbeat_handler);
  timer_mqttConnCheck.attach(2, timer_mqttConnCheck_handler);
}

#define MAX_JSON_MSG_LEN 255
StaticJsonBuffer<MAX_JSON_MSG_LEN> jsonBuffer;

char tempSendPayloadBuffer[256];
char tempReceivePayloadBuffer[MAX_JSON_MSG_LEN + 1];
char tempTopicBuffer[128];

#ifdef HAS_DHT_SENSOR
const char jsonFormatDht[] = "{\"temperature\":%s,\"humidity\":%s,\"batt\":%s}";
#endif
const char jsonFormatBattOnly[] = "{\"batt\":%s}";
const char jsonFormatErr[] = "{\"error\":\"%s\"}";
const char jsonFormatReport[] = "{\"fw\":\"%d\",\"location\":\"%s\",\"cap\":\"%s\"}";

float batt_voltage=0.00f;
char tbuff[8];
char hbuff[8];
char bvbuff[8];

//#define ONLY_SENSOR_WITH_DEEPSLEEP
#ifdef ONLY_SENSOR_WITH_DEEPSLEEP
void go_deepsleep(unsigned int seconds){
  mqtt.disconnect();
  delay(100);
  mqtt.loop();
#ifdef FORCE_DEEPSLEEP
  SERIAL_DEBUGC("Deepsleep ");
  SERIAL_DEBUGC(String(seconds));
  ESP.deepSleep(seconds * 1000000);
  delay(100);
#endif
}
#endif
/*
void handle_serial_cmd(){
  if (Serial.available() > 0) {
   String serial_rcv = Serial.readStringUntil('\r');
   Serial.println(serial_rcv);
   if (serial_rcv.substring(0,2) == "WM"){
     SERIAL_DEBUGC( "Enter WM");
      enterWifiManager();
    }
    if (serial_rcv.substring(0,2) == "RP"){
      SERIAL_DEBUG( "Read params");
      Serial.print("Log freq:");
      Serial.println(EepromConfig.settings.log_freq);
      Serial.print("IP:");
      Serial.println(WiFi.localIP());
      Serial.print("MQTT server:");
      Serial.println(EepromConfig.settings.mqtt_server);
      Serial.print("Actor:");
      Serial.println(actor_topic);
      Serial.print("Sensor:");
      Serial.println(sensor_topic);
      Serial.print("Config:");
      Serial.println(config_topic);
      Serial.print("Actor state: ");
      Serial.println(EepromConfig.settings.actor_state);
      Serial.print("Log freq");
      Serial.println(EepromConfig.settings.log_freq);
     }
  }
}
*/

void sendHeartbeat(){
  batt_voltage = ESP.getVcc()/(float)1000;
  snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/data", sensor_topic);
  sprintf(tempSendPayloadBuffer,jsonFormatBattOnly, dtostrf(batt_voltage, 6, 2, bvbuff));
  if (mqttClient.publish(tempTopicBuffer, 1, false, tempSendPayloadBuffer) == 0){
     SERIAL_DEBUGC("\n MQTT err send.") ;
  };  
  SERIAL_DEBUG("Sent HB") ;  
}

void loop(void) {
  yield();
  response_loop(100);
}

void mqtt_check_conn(){ //retry reconnect every 2 seconds  
    SERIAL_DEBUG("MQTT CKCON") ;   
    if (!mqttClient.connected()){        
        SERIAL_DEBUG("MQTT send reconnect") ;      
        mqttClient.connect();
        SERIAL_DEBUG("MQTT reconnect ok") ;      
    }
}

void response_loop(unsigned int with_wait){

  if (sendActorState){
    sendActorState = false;
    snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/state", actor_topic);
    itoa(EepromConfig.settings.actor_state,tempSendPayloadBuffer,10);
    if (mqttClient.publish(tempTopicBuffer, 1, true, tempSendPayloadBuffer) == 0){
      SERIAL_DEBUG("MQTT pub failed") ;
    };  
  }  
  if (bSendHeartbeat){
    bSendHeartbeat = false;
    sendHeartbeat();
  }

  //handle_serial_cmd();
  //inputs handling
  if (tickOccured){
    tickOccured = false;
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
    #endif
  }
  pwmval+=pwmdir;
  analogWrite(LED_BUILTIN, pwmval);
  if (pwmval >=1000) pwmdir = -20;
  if (pwmval <= 20) pwmdir = 20;
  
  if (with_wait>0){
    delay(with_wait);
  }
  if (f_reconnect){
    f_reconnect = false;
    mqtt_check_conn();
  }
  if (f_subscribe){
    f_subscribe = false;
    handleSubscribe();
  }  
  if (sem_lock_tempReceivePayloadBuffer == SEM_BUFF_AWAITPROCESS){    
    //we received a config message, process it async    
    handleConfigMsg();
    sem_lock_tempReceivePayloadBuffer = SEM_BUFF_FREE;      
  }    
  if (f_deleteRemoteConfig){
    f_deleteRemoteConfig = false;
    mqttClient.publish(config_topic, 1, false);
  }
 if (f_SendConfigReport){
    f_SendConfigReport = false;  
    sprintf(tempSendPayloadBuffer,jsonFormatReport, FW_VERSION, EepromConfig.settings.location, CAPABILITIES);
    mqttClient.publish(report_topic, 1, true, tempSendPayloadBuffer);
  }
}

void handleSubscribe(){  
  mqttClient.subscribe(actor_topic, 2);  
  mqttClient.subscribe(config_topic, 2);
}

bool handleConfigMsg(){

    if (sem_lock_tempReceivePayloadBuffer == SEM_BUFF_WRITING){
      SERIAL_DEBUG ("! HCM bufflock");  
      SERIAL_DEBUG (tempReceivePayloadBuffer);
      return false;
    }
    sem_lock_tempReceivePayloadBuffer = SEM_BUFF_READING;
    SERIAL_DEBUG ("HCM");    
    /*if (strcmp(tempReceivePayloadBuffer,"BOOT")){
      SERIAL_DEBUG ("MQTT REBOOT REQ");
      ESP.restart();
    }
    */
    SERIAL_DEBUG (tempReceivePayloadBuffer);
    JsonObject& jsonRoot = jsonBuffer.parseObject(tempReceivePayloadBuffer); //singleton holding the incoming message     
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
          SERIAL_DEBUGC ("Config report");
          f_SendConfigReport = true;
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
    SERIAL_DEBUG ("HCM ret");    
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

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  SERIAL_DEBUGC("*RX pub: ");
  SERIAL_DEBUG(topic);
  
  if (len>0){
    if (strcmp(topic, config_topic) == 0){      

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
  SERIAL_DEBUG("MqttConnect");
  SERIAL_DEBUG(sessionPresent);
  f_subscribe = true;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("Lost broker conn :");
  Serial.println((int) reason);
  SERIAL_DEBUG("Trigger mqtt reconnect");
  f_reconnect = true;
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  SERIAL_DEBUG("Sub ACK");
}

void onMqttUnsubscribe(uint16_t packetId) {
  SERIAL_DEBUG("Unsub ACK");
}

void onMqttPublish(uint16_t packetId) {
  SERIAL_DEBUG("Pub ACK");
}
