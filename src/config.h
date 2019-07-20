#ifndef config_h
#define config_h
#define SONOFF_BOARD 1
#define NODEMCU_BOARD 2

#define FW_VERSION 1244

#define BOARDTYPE SONOFF_BOARD
//#define BOARDTYPE NODEMCU_BOARD

//is a motion sensor conencted?
#define HAS_MOTION_SENSOR

//is there a button connected?
#define HAS_BUTTON

#if BOARDTYPE == NODEMCU_BOARD
  #define CAPABILITIES "NODEMCU"
  #define BOARD_CONFIG_OK
  #define ACTOR_PIN 12
  #ifdef HAS_MOTION_SENSOR
    #define MOTION_SENSOR_PIN 14
    #define MOTION_SENSOR_DEFAULT_TIMER 4
    #define CAPABILITIES CAPABILITIES " MOTION"
  #endif
  #ifdef HAS_BUTTON
    #define BUTTON_PIN 0
    #define CAPABILITIES CAPABILITIES " BUTTON"
  #endif
#endif

#if BOARDTYPE == SONOFF_BOARD
  #define BOARD_CONFIG_OK
  #define ACTOR_PIN 12
  #define LED_BUILTIN 13
  #define SONOFF_PIN_5 14
  #define SONOFF_BUTTON 0
  #define CAPABILITIES "SONOFF"
  #ifdef HAS_BUTTON
    #define BUTTON_PIN SONOFF_BUTTON
  #endif
  #ifdef HAS_MOTION_SENSOR
    #define MOTION_SENSOR_PIN SONOFF_PIN_5
    #define MOTION_SENSOR_DEFAULT_TIMER 4
  #endif
  
  #ifdef HAS_BUTTON
  #ifdef HAS_MOTION_SENSOR
  #if MOTION_SENSOR_PIN == BUTTON_PIN
  #error Button and motion sensor use the sane input pin. Check your config
  #endif
  #endif
  #endif

#endif

#ifndef BOARD_CONFIG_OK
#error Board is not configured or config wrong. Use BOARDTYPE <SONOFF_BOARD|NODEMCU_BOARD
#endif

#endif //config_h
