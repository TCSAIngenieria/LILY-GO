#include "comandos.h"  // Para acceder a getStartMarker(), getEndMarker(), getSeparator(), etc.
#include <Arduino.h>

String buffer = "";
bool receiving = false;
unsigned long ultimaLimpiezaBuffer = 0;
const unsigned long intervaloLimpieza = 2500;

static unsigned long lastCharTime = 0;
const unsigned long timeout = 100;  // 100 ms sin datos = fin de trama

extern String sensorValues[16];

void leerSensorSerial(Stream &serial) {

  // Limpiar buffer cada 2.5 segundos
  if (millis() - ultimaLimpiezaBuffer > intervaloLimpieza) {
    buffer = "";  // Vaciar buffer
    receiving = false;
    ultimaLimpiezaBuffer = millis();
  }

  // Leer caracteres entrantes
  while (serial.available()) {
    char c = serial.read();
    buffer += c;
    lastCharTime = millis();

    // Verifico si comienza una nueva trama
    if (!receiving && buffer.endsWith(getStartMarker())) {
      receiving = true;
      buffer = "";  // limpio el buffer para empezar a guardar la trama
      Serial.println("TRAMAAAAAAAA RECIBIDAAAAAAAAAAAAAA");
    }

    // Si estamos recibiendo y:
    // - hay un marcador de fin definido y se detecto
    // - o NO hay marcador de fin y paso el timeout
    bool hayEndMarker = getEndMarker().length() > 0;
    bool finPorEndMarker = receiving && hayEndMarker && buffer.endsWith(getEndMarker());
    bool finPorTimeout = receiving && !hayEndMarker && (millis() - lastCharTime > timeout);

    if (finPorEndMarker || finPorTimeout) {
      receiving = false;
      if (hayEndMarker) {
        // Quito el endMarker de la trama
        buffer.remove(buffer.length() - getEndMarker().length());
        Serial.print("endmaker");
      }

      // Parseo la trama usando el separador
      int idx = 0;
      int lastIndex = 0;
      int count = 0;

      while ((idx = buffer.indexOf(getSeparator(), lastIndex)) != -1 && count < 16) {
        sensorValues[count++] = buffer.substring(lastIndex, idx);
        lastIndex = idx + 1;
      }

      // Agrego el ultimo campo (si hay lugar)
      if (count < 16) {
        sensorValues[count++] = buffer.substring(lastIndex);
      }

      // Mostrar los valores
      Serial.println(">> Trama recibida:");
      for (int i = 0; i < count; i++) {
        Serial.print("S");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(sensorValues[i]);
      }

      // Limpio para la proxima trama
      Serial.print("<< Respuesta de expansora: ");
      Serial.println(buffer);
      buffer = "";
    }
  }
}

void imprimirSensorValuesValidos() {
  Serial.println("ultimos sensores validos recibidos:");
  for (int i = 0; i < 16; i++) {
    if (sensorValues[i].length() > 0 && sensorValues[i] != "nan") {
      Serial.print("S ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(sensorValues[i]);
    }
  }
}
