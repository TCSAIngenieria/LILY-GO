#pragma once
#include <Arduino.h>

void leerSensorSerial(Stream &serial);
void imprimirSensorValuesValidos();
void escucharBridgeSerial(Stream &serial);
