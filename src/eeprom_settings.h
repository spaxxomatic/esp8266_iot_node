#ifndef eeprom_settings_h
#define eeprom_settings_h
//############## parameters ###########
#define HAS_LOCATION 0x0b00000001
//#define HAS_SENSOR 0b00000010
#define MQTTSETTING_HAS_SERVER 0b00000100
//#define HAS_DHT_SENSOR
#define OS_MAIN_TIMER_MS 200

#define CONFIG_UNITIALIZED 0x01
#define CONFIG_SAVED 0xDD

typedef struct {
  char ssid[16];
  char password[16];
  char location[64];
  char mqtt_clientname[32];
  char mqtt_server[64];
  int mqtt_configured;
  int log_freq;
  int deepsleep;
  int actor_state;
  uint16_t update_fw_ver;
} settings_t ;

#define SSID_AUTOCONFIG "**AUTO+**"

const char PPARAM_SSID[32] PROGMEM = SSID_AUTOCONFIG;
const char PPARAM_PASSWORD[16] PROGMEM = "11213141";

#define SETTING_DEFAULT_LOGFREQ 10
#define SETTING_DEFAULT_SSID PPARAM_SSID
#define SETTING_DEFAULT_PASSWORD PPARAM_PASSWORD
#define SETTING_DEFAULT_MQTT_SERVER "192.168.1.11"

#define FLOC_EEPARAM_SAVED 0x01
#define FLOC_EEPARAM_MOTIONSENSOR_DUR 0x02
#define FLOC_EEPARAM_UPDATE_ON_RESTART 0x03
#define EEPARAM_SETTINGS_START 0x08

enum EEPROOM_HTTP_UPDATE_FLAG {
 EEPROOM_HTTP_UPDATE_DISABLED,
 EEPROOM_HTTP_UPDATE_DO_ON_REBOOT,
 EEPROOM_HTTP_NO_UPDATE_FOUND,
 EEPROOM_HTTP_UPDATE_STARTED,
 EEPROOM_HTTP_UPDATE_SUCCESS,
 EEPROOM_HTTP_UPDATE_FAILED,
};

class EepromConfigClass {
  public:
  settings_t settings;
  #ifdef HAS_MOTION_SENSOR
  uint8_t motion_sensor_off_timer = MOTION_SENSOR_DEFAULT_TIMER;
  #endif
  void begin(){ //read all params from eeprom
    EEPROM.begin(512);
    readEepromParams();
  }

  settings_t* getSettings(){
   return &settings;
  }
  void invalidateSettings(){
    EEPROM.write(FLOC_EEPARAM_SAVED, CONFIG_UNITIALIZED);
    EEPROM.commit();
  }
  void validateSettings(){
    EEPROM.write(FLOC_EEPARAM_SAVED, CONFIG_SAVED);
    EEPROM.commit();
  }

  #ifdef HAS_MOTION_SENSOR
  void storeMotionSensorOffTimer(uint8_t val){
    motion_sensor_off_timer = val;
    EEPROM.write(FLOC_EEPARAM_MOTIONSENSOR_DUR, val);
    EEPROM.commit();
  }
  #endif
  void getDefaultConfig(){
    //init with default values
    settings.log_freq = SETTING_DEFAULT_LOGFREQ;
    strcpy(settings.mqtt_server , SETTING_DEFAULT_MQTT_SERVER);
    strcpy(settings.ssid, SETTING_DEFAULT_SSID);
    strcpy(settings.password, SETTING_DEFAULT_PASSWORD);
  }

  boolean readEepromParams(){
    int isValid = EEPROM.read( FLOC_EEPARAM_SAVED );
    boolean status = false;
    if (isValid == CONFIG_SAVED){
      EEPROM.get(EEPARAM_SETTINGS_START, settings);
      if (settings.log_freq < SETTING_DEFAULT_LOGFREQ){
        settings.log_freq = SETTING_DEFAULT_LOGFREQ;
      }
      #ifdef HAS_MOTION_SENSOR
      motion_sensor_off_timer = EEPROM.read(FLOC_EEPARAM_MOTIONSENSOR_DUR);
      #endif
      status = true;
    }else{
      getDefaultConfig();
    }
    return status;
  }

  boolean set_mqtt_server(const char* mqtt_server){
    if (strlen(mqtt_server) > sizeof(settings.mqtt_server)){
      Serial.println( "mqtt_server too long");
      return false;
    }
      strcpy (settings.mqtt_server, mqtt_server);
      settings.mqtt_configured |= MQTTSETTING_HAS_SERVER;
    //EEPROM.put(EEPARAM_SETTINGS_START, settings);
    //EEPROM.commit();
    return true;
  }
  boolean store_location(const char* location){
    if (strlen(location) > sizeof(settings.location)){
      Serial.println( "Topic too long");
      return false;
    }
      strcpy (settings.location, location);
      settings.mqtt_configured |= HAS_LOCATION;
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();
    return true;
  }
  boolean store_deepsleep(int val){
    settings.deepsleep = val;
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();
    return true;
  }
  
  inline void store_update_firmware_version(uint16_t ver){    
    settings.update_fw_ver = ver;
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();    
  }

  inline void set_http_update_flag(uint8_t update_flag){    
    EEPROM.put(FLOC_EEPARAM_UPDATE_ON_RESTART, update_flag);
    EEPROM.commit();    
  }

  inline uint8_t get_http_update_flag(){    
    return EEPROM.read(FLOC_EEPARAM_UPDATE_ON_RESTART);
  }  
  
  boolean store_log_freq(int val){
    settings.log_freq = val;
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();
    return true;
  }
  
  boolean store_conn_info(const char* new_ssid, const char* new_password, const char* mqtt_server){
    Serial.println( "store_conn_info");
    if (strlen(new_ssid) > sizeof(settings.ssid)){
      Serial.println( "ssid too long");
      return false;
    }
    if (strlen(new_password) > sizeof(settings.password)){
      Serial.println( "pwd too long");
      return false;
    }
    if (! set_mqtt_server(mqtt_server) ) return false;
    strcpy (settings.ssid, new_ssid);
    strcpy (settings.password, new_password);
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();
    validateSettings();
    Serial.println( "Eeprom params saved");
    EEPROM.end();
    return true;
  }
};

EepromConfigClass EepromConfig;

#endif
