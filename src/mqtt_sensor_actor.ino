//#define SERIAL_DEBUG_ON

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "eeprom_settings.h"
#include <AsyncMqttClient.h>
#include <WiFiManager.h>
#ifdef HAS_DHT_SENSOR
#include "DHT.h"
#endif


#ifdef HAS_DHT_SENSOR
#define DHTTYPE DHT11
#define DHTPIN 13 //GPIO13
#endif

#define FORCE_DEEPSLEEP

#ifdef SERIAL_DEBUG_ON
#define SERIAL_DEBUG Serial.println
#define SERIAL_DEBUGC Serial.print
#else
#define SERIAL_DEBUG(...)
#define SERIAL_DEBUGC(...)
#endif


extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;
bool tickOccured;
// start of timerCallback
void timerCallback(void *pArg) {
      tickOccured = true;
}

void user_init(void) {

} // End of user_init


WiFiClient net;

//IPAddress mqtt_server(192, 168, 1, 11);
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
double battV;

void connect_mqtt();
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

void enterWifiManager(){
  #ifdef DEBUG_ON
    wifiManager.setDebugOutput(true);
  #endif
  wifiManager.setTimeout(1200); //wait max 10 min in config mode
  wifiManager.setSaveConfigCallback(*saveConfigParams);
  if (!wifiManager.startConfigPortal("ESP_WAITING_CONFIG", "pass")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(100);
  }
}

//typedef struct {
char actorr_topic[32] ;
char config_topic[32];
char sensor_topic[32];
//} mqtt_subscribed_topics_t;

//mqtt_subscribed_topics_t mqtt_subscribed_topics;
void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(2000);
  Serial.println();
  Serial.println("Boot...");
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(str_mac,"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2], mac[3], mac[4], mac[5]  );
  sprintf(actorr_topic, "/actor/%s", str_mac);
  sprintf(config_topic, "/config/%s", str_mac);
  sprintf(sensor_topic, "/sensor/%s", str_mac);
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


  //EepromConfig.readEepromParams();

  SERIAL_DEBUGC("Connected. IP address: ");
  SERIAL_DEBUG(WiFi.localIP());
  //mqtt config: id/name, placeholder/prompt, default, length
  ESP.wdtEnable(4000);
  pinMode(ACTOR_PIN, OUTPUT);
  pinMode(SONOFF_LED, OUTPUT);
  #ifdef HAS_BUTTON
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
  //digitalWrite(SONOFF_LED, 1);
  //pinMode(SONOFF_LED, INPUT_PULLUP);
  #ifdef HAS_DHT_SENSOR
  dht.begin();
  #endif
  mqttClient.onMessage(onMqttMessage);
  SERIAL_DEBUGC("Connecting to MQTT at ");
  SERIAL_DEBUG(EepromConfig.settings.mqtt_server);
  WiFi.hostByName(EepromConfig.settings.mqtt_server, mqttServerIp) ;
  if (mqttServerIp == IPAddress(0,0,0,0)){ //host name or ip adress is wrong, enter config
    SERIAL_DEBUG("Mqtt server config error, enter config");
    enterWifiManager();
  }
  SERIAL_DEBUG(mqttServerIp);
  mqttClient.setServer(mqttServerIp, 1883);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  //mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  //mqttClient.onPublish(onMqttPublish);
  //mqttClient.setServer(IPAddress(192, 168, 1, 11), 1883);

  //mqttClient.setKeepAlive(5).setCleanSession(false).setWill("/sensor/error", 2, true, "no").setClientId(str_mac);
  mqttClient.setKeepAlive(5).setCleanSession(false).setClientId(str_mac);
  mqttClient.connect();

  //enable PWM on led pin
  analogWriteFreq(100); // Hz freq
}


bool f_deleteRemoteConfig=false;
bool f_handleActorEvent=false;
char tempPayloadBuffer[128];
char tempTopicBuffer[128];
char jsonMsg[128];
#ifdef HAS_DHT_SENSOR
const char jsonFormatDht[] = "{\"temperature\":%s,\"humidity\":%s,\"batt\":%s}";
#endif
const char jsonFormatBattOnly[] = "{\"batt\":%s}";
const char jsonFormatErr[] = "{\"error\":\"%s\"}";
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
#define QOS_1 1

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
      Serial.print("MQTT server:");
      Serial.println(EepromConfig.settings.mqtt_server);
     }
  }
}



void loop(void) {
  int i = 0;
  response_loop(20);
  // read sensors
  batt_voltage = ESP.getVcc()/(float)1000;

  #ifdef HAS_DHT_SENSOR
    float h = dht.readHumidity()+0.1;
    delay(100);
    float t = dht.readTemperature();
    delay(100);
  #endif
  // Check if any reads failed and exit early (to try again).
  snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/data", sensor_topic);
  #ifdef HAS_DHT_SENSOR
  if (!isnan(h) || !isnan(t)) {
    //snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s%s", EepromConfig.settings.mqtt_topic, "/temp");
    //mqtt.publish(tempTopicBuffer, dtostrf(t, 1, 1, tempBuffer), true);
    sprintf(tempPayloadBuffer,jsonFormatDht, dtostrf(t, 5, 1, tbuff), dtostrf(h, 5, 1, hbuff), dtostrf(batt_voltage, 6, 1, bvbuff));
    mqttClient.publish(tempTopicBuffer, 1, false, tempPayloadBuffer);
  }else{
    sprintf(tempPayloadBuffer,jsonFormatBattOnly, dtostrf(batt_voltage, 6, 2, bvbuff));
    mqttClient.publish(tempTopicBuffer, 1, false, tempPayloadBuffer);
    //send sensor error notification
    sprintf(tempPayloadBuffer,jsonFormatErr,"Sensor error");
    snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/error", sensor_topic);
    mqttClient.publish(tempTopicBuffer, 0, false, tempPayloadBuffer);
  }
  #else
   sprintf(tempPayloadBuffer,jsonFormatBattOnly, dtostrf(batt_voltage, 6, 2, bvbuff));
   if (mqttClient.publish(tempTopicBuffer, 1, false, tempPayloadBuffer) == 0){
     SERIAL_DEBUGC("\n MQTT err send") ;
   };
  #endif
  //mqtt.publish("unconfigured/batt", dtostrf(batt_voltage, 1, 2, tempBuffer), true);
  //mqtt.publish("unconfigured/battRaw", itoa(batt, tempBuffer, 10), true);
  SERIAL_DEBUGC("\nMQTT sent to ") ;
  SERIAL_DEBUGC( tempTopicBuffer);
  SERIAL_DEBUGC( ":");
  SERIAL_DEBUG( tempPayloadBuffer) ;
  //mqtt.publish("unconfigured/resetReason", ESP.getResetReason());
  if (EepromConfig.settings.deepsleep > 0){
    //let ESP go to deep sleep
  }

  response_loop(100);
  for(int i = 0; i < EepromConfig.settings.log_freq; i++){
    for (int j= 0; j < 10; j++){
      response_loop(100);
    }
  }
}

void response_loop(unsigned int with_wait){
  static bool bBlink = false;
  static uint pwmval = 1;
  handle_serial_cmd();
  //inputs handling
  if (tickOccured){
    tickOccured = false;
    bBlink = true;
    //we read the button state only once every tick, (debouncing)
    #ifdef HAS_BUTTON
    #ifdef HAS_MOTION_SENSOR
    #error Motion sensor and button cannot be active simultaneouly, check your config
    #endif
    uint8_t val = digitalRead(BUTTON_PIN);  // read input value
     if (val == HIGH) {         // check if the input is HIGH (button released)
       iButtonState = 0;
     } else {
       iButtonState += 1;
       //now the action on button press
     }
     //if (bButtonState == 1 && bPrevButtonState != bButtonState  ){
     if (iButtonState == 1){
       //only the first press after a depress shall trigger a state change
       EepromConfig.settings.actor_state = !EepromConfig.settings.actor_state ;
       f_handleActorEvent = true;
       //bPrevButtonState = bButtonState;
     }
    #endif
    #ifdef HAS_MOTION_SENSOR
    #ifdef HAS_BUTTON
    #error Motion sensor and button cannot be active simultaneouly, check your config
    #endif
      uint8_t val = digitalRead(MOTION_SENSOR_PIN);  // read input value
      //SERIAL_DEBUG(cnt_motion_sensor_countdown);
      if (val == LOW) {         // No motion
           if (cnt_motion_sensor_countdown != 0xFF){
             //we need this check so that the events are generated only once per trigger
             //when the counter reaches 0, no further events shall be generated
             if (cnt_motion_sensor_countdown > 0) cnt_motion_sensor_countdown -= 1;
             else {
               //the cnt_motion_sensor_timer == 0 is used as action marker.
               //We set the value to 0xFF afterwards so that no further events are raised
               EepromConfig.settings.actor_state = 0;
               cnt_motion_sensor_countdown = 0xFF;
               f_handleActorEvent = true;
             }
           }
      } else {
            if (EepromConfig.settings.actor_state == 0) {
             //if not already active
               EepromConfig.settings.actor_state = 1;
               f_handleActorEvent = true;
            }
            cnt_motion_sensor_countdown = (1000/OS_MAIN_TIMER_MS)*EepromConfig.motion_sensor_off_timer;
      }
    #endif
  }
  pwmval+=pwmdir;
  analogWrite(SONOFF_LED, pwmval);
  if (pwmval >=1020) pwmdir = -10;
  if (pwmval <= 10) pwmdir = 10;
  if (bBlink) {
    bBlink = false;
    //digitalWrite(SONOFF_LED, EepromConfig.settings.actor_state);
    //delay(5);
    //digitalWrite(SONOFF_LED, !EepromConfig.settings.actor_state);
  }
  if (with_wait>0){
    delay(with_wait);
  }
  //delete config, if it needs to be deleted
  if (f_deleteRemoteConfig){
    mqttClient.publish(config_topic, 1, false);
    f_deleteRemoteConfig = false;
  }

  if (f_handleActorEvent){
    digitalWrite(ACTOR_PIN, EepromConfig.settings.actor_state );
    //digitalWrite(SONOFF_LED, !EepromConfig.settings.actor_state );
    snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/state", actorr_topic);
    itoa(EepromConfig.settings.actor_state,tempPayloadBuffer,10);
    mqttClient.publish(tempTopicBuffer, 1, true, tempPayloadBuffer);
    f_handleActorEvent = false;
    SERIAL_DEBUGC("\nMQTT sent to ") ;
    SERIAL_DEBUGC( tempTopicBuffer);
    SERIAL_DEBUGC( ":");
    SERIAL_DEBUG( tempPayloadBuffer) ;
  }
}

bool msg_received = false;
void handleConfigMsg(char* payload, unsigned int length){
    StaticJsonBuffer<200> jsonBuffer;
    SERIAL_DEBUG("handleConfigMsg");
    SERIAL_DEBUG(payload);
    if (length >= sizeof(jsonBuffer)){
      return ;
    }
    JsonObject& root = jsonBuffer.parseObject(payload);
    // Test if parsing succeeds.
    if (!root.success()) {
        SERIAL_DEBUG("Payload parse fail");
      //return;
    }else{
        int deepsleep = root["deepsleep"];
        if (deepsleep > 0){
            EepromConfig.store_deepsleep(deepsleep);
          }else{
            SERIAL_DEBUG ("No deepsleep value");
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
        #ifdef HAS_MOTION_SENSOR
        int val = root["motion_sensor_timer"];
        if (val){
          SERIAL_DEBUGC ("Set motion_sensor_timer: ");
          SERIAL_DEBUG (val);
          if (val >= MOTION_SENSOR_DEFAULT_TIMER){
            EepromConfig.storeMotionSensorOffTimer(val);
          }else{
            SERIAL_DEBUG ("No mst val or invalid");
          }
        }
        #endif
      f_deleteRemoteConfig = true;
    }

}

void handleActorMsg(char* payload, int length){
  SERIAL_DEBUG ("handleActorMsg");
  SERIAL_DEBUG ((char *) payload);
  if (length > 0){
    f_handleActorEvent = true;
    if (payload[0] == '0'){
      EepromConfig.settings.actor_state = 0;
    }else{
      EepromConfig.settings.actor_state = 1;
    }
    //snprintf(tempTopicBuffer, sizeof(tempTopicBuffer), "%s/state", actorr_topic);
    //mqttClient.publish(tempTopicBuffer, 1, true, payload);
  }

}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  SERIAL_DEBUGC("** Pub rcv:");
  SERIAL_DEBUG(topic);
  SERIAL_DEBUG(len);
  //payload[len] = '\0';
  if (len>0){
    //SERIAL_DEBUG("%.*s", 4, buff + 10));
    if (strcmp(topic, config_topic) == 0){
      handleConfigMsg(payload, len);
    }else if (strcmp(topic, actorr_topic) == 0){
      handleActorMsg(payload, len);
    }
  }
  msg_received=true;
}


void onMqttConnect(bool sessionPresent) {
  SERIAL_DEBUG("** Connected to broker");
  //SERIAL_DEBUG("Session ");
  //SERIAL_DEBUG(sessionPresent);
  //mqttClient.subscribe(config_topic, 2);
  //SERIAL_DEBUG("Subscribing to ");
  //SERIAL_DEBUG(actorr_topic);
  mqttClient.subscribe(actorr_topic, 2);
  //SERIAL_DEBUG(config_topic);
  mqttClient.subscribe(config_topic, 2);

  //Serial.print("Subscribing at QoS 2, packetId: ");
  //Serial.println(packetIdSub);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("** Lost broker conn");
  Serial.println((int) reason);
  Serial.println("Reconnecting to MQTT...");
  mqttClient.connect();
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("** Sub ACK");
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("** Unsub ACK");
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("** Pub ACK");
}
