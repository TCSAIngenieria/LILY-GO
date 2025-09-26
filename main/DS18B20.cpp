#include "DS18B20.h"
#include <Arduino.h>

DS18B20::DS18B20() : oneWire(ONE_WIRE_BUS), sensors(&oneWire) {}

void DS18B20::begin() {
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(500);
  initSensor();
}

void DS18B20::initSensor() {
  
  
  sensors.begin();
  if (!sensors.getAddress(address, 0)) {
    Serial.println("❌ No se encontró el sensor DS18B20.");
    sensorEncontrado = false;
  } else {
    Serial.println("✅ Sensor DS18B20 encontrado.");
    sensorEncontrado = true;
  }
}

float DS18B20::readValue() {
  if (!sensorEncontrado) {
    Serial.println("❌ Sensor no detectado. Reiniciando...");
    return -999.0;
  }

  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);

  // Controla que la lectura no sea error por desconexión o valores inválidos típicos
  if (temp == DEVICE_DISCONNECTED_C || temp == -127.0 || temp < -55.0 || temp > 85.0) {
    Serial.println("⚠️ Error leyendo la temperatura. Devolviendo última válida.");
    return ultimaTempValida;
  } else {
    // Actualiza última válida solo si cambió más de 0.01 grados
    if (abs(temp - ultimaTempValida) > 0.01) {
      ultimaTempValida = temp;
      tiempoUltimoCambio = millis();
    }
    return temp;
  }
}

String DS18B20::getString() {
  return String(ultimaTempValida, 2);
}

void DS18B20::loop() {
  unsigned long now = millis();

  switch (estadoReinicio) {
    case IDLE:
      if ((now - tiempoUltimoCambio > SENSOR_TIEMPO_MAX_IGUAL) ||
          ultimaTempValida == -127.0 || ultimaTempValida == -999.0) {
        Serial.println("🔁 Reiniciando sensor DS18B20...");
        digitalWrite(SENSOR_POWER_PIN, LOW);
        tiempoReinicio = now;
        estadoReinicio = APAGADO;
      }
      break;

    case APAGADO:
      if (now - tiempoReinicio > 1000) {
        digitalWrite(SENSOR_POWER_PIN, HIGH);
        tiempoReinicio = now;
        estadoReinicio = ENCENDIDO;
      }
      break;

    case ENCENDIDO:
  if (now - tiempoReinicio > 500) {
    Serial.println("✅ Sonda DS18B20 reiniciada.");
    tiempoUltimoCambio = now;
    initSensor();  
    estadoReinicio = IDLE;
  }
  break;
  }
}
