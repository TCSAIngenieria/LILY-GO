#ifndef COMANDOS_H
#define COMANDOS_H

#include <Arduino.h>  // <- NECESARIO para que reconozca String, etc.


String procesarComando(String comando);
String getStartMarker();
String getEndMarker();
String* getSensorValues();
char getSeparator();
void escucharComandos();
void guardarConfiguracionParser();
void cargarConfiguracionParser();

// Configuracion compartida de UART2 y paridad guardada para Modbus.
void cargarConfiguracionUART2();
bool iniciarUART2(bool modoModbus);
const char *obtenerParidadModbus();
void cargarConfiguracionEnvioModbus();
bool envioModbusEnCola();

#endif
