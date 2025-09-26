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

#endif
