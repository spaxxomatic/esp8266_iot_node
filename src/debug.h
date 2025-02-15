#ifndef debug_h
#define debug_h

#include "config.h"

#define SERIAL_DEBUG_ON

#ifdef SERIAL_DEBUG_ON
#define SERIAL_DEBUG Serial.println
#define SERIAL_DEBUGC Serial.print
#else
#define SERIAL_DEBUG(...)
#define SERIAL_DEBUGC(...)
#endif

#define DEBUG_ON

#ifdef HAS_OLED_DISPLAY
#define DISPLAY_STATUS_ON

#ifdef DISPLAY_STATUS_ON
#define DISPLAY_DEBUG oled.print()
#endif

#endif
#endif //debug_h
