#ifndef debug_h
#define debug_h
#define SERIAL_DEBUG_ON

#ifdef SERIAL_DEBUG_ON
#define SERIAL_DEBUG Serial.println
#define SERIAL_DEBUGC Serial.print
#else
#define SERIAL_DEBUG(...)
#define SERIAL_DEBUGC(...)
#endif

#endif //debug_h
