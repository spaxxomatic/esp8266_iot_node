// #define BOARDTYPE SONOFF
// #define BOARDTYPE NODEMCU

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
