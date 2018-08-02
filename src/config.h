#ifndef config_h
#define config_h
#define SONOFF_BOARD 1
#define NODEMCU_BOARD 2


//#define BOARDTYPE SONOFF_BOARD
#define BOARDTYPE NODEMCU_BOARD

//is a motion sensor conencted?
//#define HAS_MOTION_SENSOR

//is there a button conencted? (mutually exclusive with HAS_MOTION_SENSOR)
#define HAS_BUTTON

#if BOARDTYPE == SONOFF
#ifdef HAS_BUTTON
#ifdef HAS_MOTION_SENSOR
#error For sonoff board, only one input is available. Do not activate both motion sensor and button !
#endif
#endif
#endif


#if BOARDTYPE == NODEMCU_BOARD
  #define BOARD_CONFIG_OK
  #define ACTOR_PIN 12
  #ifdef HAS_MOTION_SENSOR
    #define MOTION_SENSOR_PIN 14
    #define MOTION_SENSOR_DEFAULT_TIMER 4
  #endif
//#define HAS_BUTTON
  #ifdef HAS_BUTTON
  #define BUTTON_PIN 0
  #endif
#endif

#if BOARDTYPE == SONOFF_BOARD
  #define BOARD_CONFIG_OK
  #define ACTOR_PIN 12
  #define LED_BUILTIN 13
  #define SONOFF_PIN_5 14

  #ifdef HAS_BUTTON
    #define BUTTON_PIN SONOFF_PIN_5
  #endif
  #ifdef HAS_MOTION_SENSOR
    #define MOTION_SENSOR_PIN SONOFF_PIN_5
    #define MOTION_SENSOR_DEFAULT_TIMER 4
  #endif
#endif

#ifndef BOARD_CONFIG_OK
#error Board is not configured or config wrong. Use BOARDTYPE <SONOFF_BOARD|NODEMCU_BOARD
#endif

#endif //config_h
