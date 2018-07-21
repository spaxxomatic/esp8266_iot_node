#ifndef config_h
#define config_h
#define SONOFF_BOARD 1
#define NODEMCU_BOARD 2

//#define BOARDTYPE SONOFF_BOARD
#define BOARDTYPE NODEMCU_BOARD

//is a motion sensor conencted?
#define HAS_MOTION_SENSOR

//is there a button conencted? (mutually exclusive with HAS_MOTION_SENSOR)
//#define HAS_BUTTON

#if BOARDTYPE == SONOFF
#ifdef HAS_BUTTON
#ifdef HAS_MOTION_SENSOR
#error For sonoff board, only one input is available. Do not activate both motion sensor and button !
#endif
#endif
#endif

#endif //config_h
