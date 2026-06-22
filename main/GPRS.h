#ifndef GPRS_H
#define GPRS_H

#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024  // Set RX buffer to 1Kb

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <SPI.h>
#include <SD.h>

// Pines y configuraciones de hardware
#define UART_BAUD 115200
#define PIN_DTR 25
#define PIN_TX 27
#define PIN_RX 26
#define PWR_PIN 4

#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS 13
#define LED_PIN 12

extern HardwareSerial SerialAT;

void modemPowerOn();
void modemPowerOff();
void modemRestart();
bool isModemOn();
void asegurarModemEncendido();
void iniciarSerialModem();
void initSD();
void printModemInfo(TinyGsm &modem, String &res);
void updateNetworkConnection(TinyGsm &modem);
bool updateClockFromNTP(TinyGsm &modem);
void updateInternalClock(String clockString);
String printCurrentTime();
String getGSMTech();
void getModemSignalInfo(TinyGsm &modem, String &rsrq, String &rsrp, String &rssi);

#endif
