#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <Arduino.h>

// Solo guarda avisos recibidos por UART. El procesamiento ocurre en loop().
void clockTimeNotice(const String &line);
void clockModemReset();

#endif
