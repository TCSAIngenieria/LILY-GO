#pragma once
#include <Arduino.h>

void iniciarBridgeAP();
void mantenerBridgeAP();
bool agregarComandoCola(String ident, String mensaje);
String obtenerComandoCola(String ident);

