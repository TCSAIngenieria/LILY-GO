#ifndef FECHAYHORA_H
#define FECHAYHORA_H


#include <Arduino.h>
#include "esp_system.h"
#include "esp_task_wdt.h"


void updateClockFromNTP_wifi();
void updateClockFromBridge();
String printCurrentTime();
bool isTimeSet();

#endif
