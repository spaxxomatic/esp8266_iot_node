#ifndef eeprom_settings_h
#define eeprom_settings_h
//############## parameters ###########
#define HAS_LOCATION 0x0b00000001
//#define HAS_SENSOR 0b00000010
#define MQTTSETTING_HAS_SERVER 0b00000100
//#define HAS_DHT_SENSOR
#define OS_MAIN_TIMER_MS 100

#define CONFIG_UNITIALIZED 0x01
#define CONFIG_SAVED 0xDD

#ifdef ESP32
#include <esp_task_wdt.h>
#endif 

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
  uint32_t motion_sensor_off_timer;
  char mqtt_username[32];
  char mqtt_password[32];
} settings_t ;


#define SSID_AUTOCONFIG "**AUTO+**"

const char PPARAM_SSID[32] PROGMEM = SSID_AUTOCONFIG;
const char PPARAM_PASSWORD[16] PROGMEM = "11213141";

#define SETTING_DEFAULT_LOGFREQ 60
#define SETTING_MIN_OFF_TIMER 5
#define SETTING_DEFAULT_SSID PPARAM_SSID
#define SETTING_DEFAULT_PASSWORD PPARAM_PASSWORD
#define SETTING_DEFAULT_MQTT_SERVER "192.168.1.5"
#define SETTING_DEFAULT_MQTT_USERNAME "mqttactor"
#define SETTING_DEFAULT_MQTT_PASSWORD "mqttpass"
#define FLOC_EEPARAM_SAVED 0x01
#define FLOC_EEPARAM_MOTIONSENSOR_DUR 0x02
#define FLOC_EEPARAM_UPDATE_ON_RESTART 0x03

#define EEPARAM_SETTINGS_START 0x08

#define EEPARAM_LASTERR_ADDR EEPARAM_SETTINGS_START + sizeof(settings_t) + 1
#define EEPARAM_LASTERR_LEN 64

#define EEPARAM_SIZE EEPARAM_LASTERR_ADDR + EEPARAM_LASTERR_LEN

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
  
  void begin(){ //read all params from eeprom
    EEPROM.begin(EEPARAM_SIZE);
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
  void storeMotionSensorOffTimer(uint32_t val){
    
    settings.motion_sensor_off_timer = val;
    EEPROM.put(EEPARAM_SETTINGS_START + offsetof(settings_t, motion_sensor_off_timer), settings.motion_sensor_off_timer);
    EEPROM.commit();
  }
  #endif
  void getDefaultConfig(){
    //init with default values
    settings.log_freq = SETTING_DEFAULT_LOGFREQ;
    settings.motion_sensor_off_timer = SETTING_MIN_OFF_TIMER;
    strcpy(settings.mqtt_server , SETTING_DEFAULT_MQTT_SERVER);
    strcpy(settings.mqtt_username , SETTING_DEFAULT_MQTT_USERNAME);
    strcpy(settings.mqtt_password , SETTING_DEFAULT_MQTT_PASSWORD);
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
      if (settings.motion_sensor_off_timer < SETTING_MIN_OFF_TIMER){
        settings.motion_sensor_off_timer = SETTING_MIN_OFF_TIMER;
      }

      status = true;
    }else{
      getDefaultConfig();
    }
    return status;
  }

  boolean set_mqtt_server(const char* mqtt_server, const char* mqtt_username, const char* mqtt_password){
    if (strlen(mqtt_server) > sizeof(settings.mqtt_server)){
      SERIAL_DEBUG( "mqtt_server too long");
      return false;
    }
    if (strlen(mqtt_username) > sizeof(settings.mqtt_username)){
      SERIAL_DEBUG( "mqtt_user too long");
      return false;
    }
    if (strlen(mqtt_password) > sizeof(settings.mqtt_password)){
      SERIAL_DEBUG( "mqtt_pwd too long");
      return false;
    }        
      strcpy (settings.mqtt_server, mqtt_server);
      strcpy (settings.mqtt_username, mqtt_username);
      strcpy (settings.mqtt_password, mqtt_password);
      settings.mqtt_configured |= MQTTSETTING_HAS_SERVER;
    //EEPROM.put(EEPARAM_SETTINGS_START, settings);
    //EEPROM.commit();
    return true;
  }

  boolean store_location(const char* location){
    if (strlen(location) > sizeof(settings.location)){
      SERIAL_DEBUG( "Location too long");
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
    EEPROM.write(FLOC_EEPARAM_UPDATE_ON_RESTART, update_flag);
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
  
  boolean store_conn_info(const char* new_ssid, const char* new_password, const char* mqtt_server, const char* mqtt_user, const char* mqtt_pass){
    Serial.println( "store_conn_info");
    if (strlen(new_ssid) > sizeof(settings.ssid)){
      SERIAL_DEBUG( "ssid too long");
      return false;
    }
    if (strlen(new_password) > sizeof(settings.password)){
      SERIAL_DEBUG( "pwd too long");
      return false;
    }
    if (! set_mqtt_server(mqtt_server, mqtt_user, mqtt_pass) ) return false;    
    strcpy (settings.ssid, new_ssid);
    strcpy (settings.password, new_password);
    EEPROM.put(EEPARAM_SETTINGS_START, settings);
    EEPROM.commit();
    validateSettings();
    Serial.println( "Eeprom params saved");
    return true;
  }

#ifdef ESP32
  boolean store_lasterr(const char* errstr){
    int ilen = strlen(errstr) ;
    if (ilen > EEPARAM_LASTERR_LEN){
      SERIAL_DEBUG( "erstr too long");
      return false;
    }
    EEPROM.writeString(EEPARAM_LASTERR_ADDR, errstr);    
    EEPROM.commit();
    return true;
  }  
  
  inline String get_lasterr(){
    return EEPROM.readString(EEPARAM_LASTERR_ADDR);
  }
#else
//the ESP8266 lib does not have the read/writestring methods
String get_lasterr ()
{
  uint16_t len = EEPARAM_LASTERR_LEN;

  char value[EEPARAM_LASTERR_LEN];
  
  for (int i= 0; i < len; i++){
    value[i] = EEPROM.read( EEPARAM_LASTERR_ADDR + i);
  }
  
  value[len] = 0; //just to be safe, in case the mem is not initialized
  
  return String(value);
}

size_t store_lasterr (const char* value)
{
  if (!value)
    return 0;

  uint16_t len = strlen(value) + 1 ;
  if (len > EEPARAM_LASTERR_LEN) {
    len = EEPARAM_LASTERR_LEN;
  }
  
  for (int i= 0; i < len; i++){
    EEPROM.write( EEPARAM_LASTERR_ADDR,  value[i]);
  }
  EEPROM.write(EEPARAM_LASTERR_ADDR + len , 0); //string terminator
  
  EEPROM.commit();
  return len;
}
#endif


};

EepromConfigClass EepromConfig;


  int command_set_conn_params (char* params, Stream* stream){
		//char * ssid; char* pwd; char* mqtt_addr; char* mqtt_user; char* mqtt_pass;
		  stream->print("Setting connection ");		
      stream->println(params);		

		  char *substrings[5];
    	char *token;
    	int i = 0;

    	// Tokenize the string
    	token = strtok(params, " ");
    	while (token != NULL && i < 5) {
        	substrings[i] = token;
        	token = strtok(NULL, " ");
        	i++;
    	}
		  
      if (i < 4) {
    		stream->println("Some param missing, please check connection string");
    		return -1;
    	}
		
  		if (!EepromConfig.store_conn_info(substrings[0], substrings[1],substrings[2], substrings[3], substrings[4]))    	
    	{
      		stream->println("Eeprom save failed");
          return -1;
    	};
      return 0;
	 }

    void command_set_conn_params (char* params){
      command_set_conn_params(params, &Serial);
    }

    void esp_bugfree_restart(){
      // implementing some of the tips from https://github.com/esp8266/Arduino/issues/1722 to avoid hanging restarts
      //.... I set GPIO#0 high before doing the restart and it works!
      pinMode(0, INPUT_PULLUP);
      
      #ifdef ESP32
      ESP.restart();
      #else
      WiFi.forceSleepBegin();
      delay(100);
      wdt_reset(); 
      ESP.restart(); 
      while( 1) wdt_reset();
      #endif
    }

    void command_trigger_update(uint32 fw_version){
        if (fw_version <= FW_VERSION){
          Serial.printf("Version %i older than actual version %i \n", fw_version, FW_VERSION);
          return ;
        };
        EepromConfig.set_http_update_flag(EEPROOM_HTTP_UPDATE_DO_ON_REBOOT);        
        EepromConfig.store_update_firmware_version(fw_version);
        esp_bugfree_restart();
    } 

    void command_trigger_update(char* params){
        
        uint32 fw_update_ver = atoi(params);
        command_trigger_update(fw_update_ver);
    }


#endif
