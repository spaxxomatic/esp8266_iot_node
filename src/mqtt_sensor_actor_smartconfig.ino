
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
#include<esp8266httpupdate.h>
//#include <ArduinoOTA.h>
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;

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
bool tickOccured;
bool f_SendConfigReport;
bool f_StartHttpUpdate;
// start of timerCallback
void timerCallback(void *pArg) {
      tickOccured = true;
}

WiFiClient net;

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

bool f_deleteRemoteConfig=false;
bool f_handleActorEvent=false;
char tempPayloadBuffer[256];
char tempTopicBuffer[128];
char jsonMsg[128];

void connect_mqtt();
ADC_MODE(ADC_VCC);

char str_mac[16] ;

void saveConfigParams() {
  String ssid = WiFi.SSID();
  String pass = WiFi.psk();
  SERIAL_DEBUG(" cb: save config");
  SERIAL_DEBUG(WiFi.SSID());
  SERIAL_DEBUG(WiFi.psk());
  
  //EepromConfig.store_conn_info(WiFi.SSID().c_str(), WiFi.psk().c_str());
  if (!EepromConfig.store_conn_info(ssid.c_str(), pass.c_str(), "192.168.1.11")){
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

void enterConfig(){
  blinker.attach(0.3, flip);
  //try first default password on the ssid with the best signal
 WiFi.mode(WIFI_STA);
  wifiMulti.addAP("asus", "11213141");
  wifiMulti.addAP("fasole", "11213141");
  wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");

  #ifdef DEBUG_ON
    wifiManager.setDebugOutput(true);
  #endif
 if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected:");
    Serial.println(WiFi.localIP());
  }else{ //enter smart config
    delay(100);
    WiFi.beginSmartConfig();

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      Serial.println(WiFi.smartConfigDone());
    }    
  }
  if (WiFi.status() == WL_CONNECTED) saveConfigParams();
  blinker.detach();
}

void httpUpdate(){
    /* warning - the update is done after each reboot now */
  
  const char upd_url_template[] = "http://%s:8266/fw_%i.bin";
  sprintf(tempPayloadBuffer,upd_url_template, EepromConfig.settings.mqtt_server, FW_VERSION + 1);
  Serial.print("Load fw ");
  Serial.println(tempPayloadBuffer);
  t_httpUpdate_return ret = ESPhttpUpdate.update(tempPayloadBuffer); //Location of your binary file
  //t_httpUpdate_return  ret = ESPhttpUpdate.update("https://server/file.bin");
  /*upload information only */
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAIL Err (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
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
    pwmval = 1024; //
    digitalWrite(ACTOR_PIN, EepromConfig.settings.actor_state );
    //digitalWrite(SONOFF_LED, !EepromConfig.settings.actor_state );
    sendActorState = true;
}
void actor_on(bool keep_on){
    EepromConfig.settings.actor_state = 1;
    set_actor();
    if (! keep_on)
      timer_light_on.once(EepromConfig.motion_sensor_off_timer, actor_off);
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
  Serial.print("FW ver ");
  Serial.println(FW_VERSION);
  f_StartHttpUpdate = false;
  pinMode(LED_BUILTIN, OUTPUT);
  // flip the pin every 0.3s
  //httpUpdate();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
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
              Serial.println("WiFi fail, enter config");
              enterConfig();
            };
    }
  }else{
    //no valid  eprom config, try to connect with default values
    //EepromConfig.getDefaultConfig();
    //go in config mode
    enterConfig();
  }
  //if we reached this place, we're connected
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());
  WiFi.onEvent(WiFiEvent);

  //mqtt config: id/name, placeholder/prompt, default, length
  ESP.wdtEnable(4000);
  pinMode(ACTOR_PIN, OUTPUT);
  #ifdef HAS_BUTTON
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
  //digitalWrite(SONOFF_LED, 1);
  //pinMode(SONOFF_LED, INPUT_PULLUP);
  #ifdef HAS_DHT_SENSOR
  dht.begin();
  #endif
  mqttClient.onMessage(onMqttMessage);
  //Serial.print("Connect MQTT srv ");
  //Serial.println(EepromConfig.settings.mqtt_server);
  WiFi.hostByName(EepromConfig.settings.mqtt_server, mqttServerIp) ;
  if (mqttServerIp == IPAddress(0,0,0,0)){ //host name or ip adress is wrong, enter config
    Serial.println("Mqtt server config error, enter config");
    enterConfig();
  }
  Serial.println(mqttServerIp);
  mqttClient.setServer(mqttServerIp, 1883);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setKeepAlive(5).setCleanSession(false).setClientId(str_mac);
  mqttClient.connect();
  //enable PWM on led pin
  
  analogWriteFreq(100); // Hz freq
  timer_heartbeat.attach(EepromConfig.settings.log_freq, timer_heartbeat_handler);
}

#ifdef HAS_DHT_SENSOR
const char jsonFormatDht[] = "{\"temperature\":%s,\"humidity\":%s,\"batt\":%s}";
#endif
const char jsonFormatBattOnly[] = "{\"batt\":%s}";
const char jsonFormatErr[] = "{\"error\":\"%s\"}";
const char jsonFormatReport[] = "{\"fw\":\"%s\",\"location\":\"%s\",\"cap\":\"%s\"}";

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
  sprintf(tempPayloadBuffer,jsonFormatBattOnly, dtostrf(batt_voltage, 6, 2, bvbuff));
  if (! mqttClient.connected()){
    onMqttDisconnect(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
  }
  if (mqttClient.publish(tempTopicBuffer, 1, false, tempPayloadBuffer) == 0){
       SERIAL_DEBUGC("\n MQTT err send.") ;
  };  
  SERIAL_DEBUG("Sent HB") ;  
}

void loop(){
  if (!mqttClient.connected()){
      SERIAL_DEBUG("MQTT reconnect.. ") ;
      delay(200);
      mqttClient.connect();
  }
  yield();
  if (sendActorState){
    sendActorState = false;
    snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/state", actor_topic);
    itoa(EepromConfig.settings.actor_state,tempPayloadBuffer,10);
    if (mqttClient.publish(tempTopicBuffer, 1, true, tempPayloadBuffer) == 0){
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
  delay(50);
  //we just received a config message, delete it from the broker
  if (f_deleteRemoteConfig){
    mqttClient.publish(config_topic, 1, false);
    f_deleteRemoteConfig = false;
  }
 if (f_SendConfigReport){
    sprintf(tempPayloadBuffer,jsonFormatReport, FW_VERSION, EepromConfig.settings.location, CAPABILITIES);
    mqttClient.publish(report_topic, 1, true, tempPayloadBuffer);
    f_SendConfigReport = false;    
  }
  if (f_StartHttpUpdate){
    f_StartHttpUpdate = false;
    httpUpdate();
  }
}


void handleConfigMsg(char* payload, unsigned int length){
    StaticJsonBuffer<200> jsonBuffer;
    
    SERIAL_DEBUG("handleConfigMsg");
    SERIAL_DEBUG(payload);
    if (length <= 1) {
      SERIAL_DEBUG(" ! No payload");
      return ;
    }
    if (length >= sizeof(jsonBuffer)){
      return ;
    }
    if (strcmp(payload,"UPDATE")){
      f_StartHttpUpdate = true;
    }else{
      JsonObject& root = jsonBuffer.parseObject(payload);
      // Test if parsing succeeds.
      if (!root.success()) {
          SERIAL_DEBUG("Payload parse fail");
        //return;
      }else{
        int deepsleep = root["deepsleep"];
        if (deepsleep > 0){
            EepromConfig.store_deepsleep(deepsleep);
            SERIAL_DEBUGC ("Deepsleep value set to ");
            SERIAL_DEBUG (deepsleep);
        }
        const char*  location = root["location"];
        //root.prettyPrintTo(Serial);
        if (location){
          SERIAL_DEBUG ("Set location");
          EepromConfig.store_location(location);
          //delete config and delete it
        }
        int log_freq = root["log_freq"];
        if (log_freq){
          SERIAL_DEBUGC ("Set log_freq");
          SERIAL_DEBUG (log_freq);
          if (log_freq > 0){
            EepromConfig.store_log_freq(log_freq);
          }else{
            SERIAL_DEBUG ("No freq value");
          }
        }
        int report = root["report"];
        if (report){
          SERIAL_DEBUGC ("Config report");
          f_SendConfigReport = true;
          return;
        }        
        #ifdef HAS_MOTION_SENSOR
        int val = root["motion_sensor_timer"];
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
      }
      f_deleteRemoteConfig = true;
    }
}

void handleActorMsg(char* payload, int length){
  SERIAL_DEBUG ("handleActorMsg");
  SERIAL_DEBUG ((char *) payload);
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
  SERIAL_DEBUGC("*RX pub");
  SERIAL_DEBUG(topic);
  SERIAL_DEBUG(len);
  //payload[len] = '\0';
  if (len>0){
    if (strcmp(topic, config_topic) == 0){
      handleConfigMsg(payload, len);
    }else if (strcmp(topic, actor_topic) == 0){
      handleActorMsg(payload, len);
    }
  }
}


void onMqttConnect(bool sessionPresent) {
  SERIAL_DEBUG("MqttConnect");
  SERIAL_DEBUG(sessionPresent );
  mqttClient.subscribe(actor_topic, 2);
  mqttClient.subscribe(config_topic, 2);
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case WIFI_EVENT_STAMODE_DISCONNECTED:
        Serial.println("WiFi lost connection");
        //xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		    //xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("Lost broker conn");
  Serial.println((int) reason);
  SERIAL_DEBUG("Reconnecting to MQTT...");
  if (WiFi.isConnected()) {
    //xTimerStart(mqttReconnectTimer, 0);
    mqttClient.connect(); 
    
  }else{
    Serial.print("Lost wifi conn");
  }
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
