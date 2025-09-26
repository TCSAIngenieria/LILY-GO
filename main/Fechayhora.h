#ifndef FECHAYHORA_H
#define FECHAYHORA_H


#include <Arduino.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

// Pines y configuraciones de hardware
#define UART_BAUD   115200
#define PIN_DTR     25
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4

#define SD_MISO     2
#define SD_MOSI     15
#define SD_SCLK     14
#define SD_CS       13
#define LED_PIN     12


void updateClockFromNTP_wifi();
String printCurrentTime();
bool isTimeSet();

#endif
