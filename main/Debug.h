#ifndef DVL_DEBUG_H
#define DVL_DEBUG_H

#include <Arduino.h>

// Set this to 0 to disable all console debug output globally
// Set this to 1 to enable console debug output globally
#define DEBUG_MODE 1

#if DEBUG_MODE
    #define DVL_PRINT(x)       Serial.print(x)
    #define DVL_PRINTLN(x)     Serial.println(x)
    #define DVL_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
    #define DVL_PRINT(x)
    #define DVL_PRINTLN(x)
    #define DVL_PRINTF(fmt, ...)
#endif

#endif // DVL_DEBUG_H
