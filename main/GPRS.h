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

// Configuraciones de diagnostico y tecnologia del modem
extern uint8_t modemDebugLevel; // 0=Normal, 1=Detallado, 2=Detallado + Raw AT
extern uint8_t modemTechMode;   // 0=Auto (CAT-M1 -> NB-IoT -> 2G), 1=Fijo CAT-M1, 2=Fijo NB-IoT, 3=Fijo 2G
extern bool isModemReady;
extern bool isModemConnected;
extern bool isGprsActive;

// Interceptor de Stream para visualizar comandos AT crudos en tiempo real cuando modemDebugLevel == 2
class ModemDebugStream : public Stream {
public:
  ModemDebugStream(Stream &s) : _stream(s) {}
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

private:
  Stream &_stream;
  String _txLine;
  String _rxLine;
};

extern ModemDebugStream modemStream;

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

// Nuevas funciones de diagnostico y control de conexion celular
void forceModemTechReconfig();
void notifyModemReset();
void preconfigureModemAPN(TinyGsm &modem, const String &apnStr);
String getModemTechModeName(uint8_t mode);
String getModemDetailedStatus(TinyGsm &modem);

#endif
