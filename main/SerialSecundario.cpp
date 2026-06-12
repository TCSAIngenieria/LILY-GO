#include "comandos.h"  // Para acceder a getStartMarker(), getEndMarker(), getSeparator(), etc.
#include "Debug.h"
#include <Arduino.h>
#include "MQTT.h"
#include "Flash.h"
#include <ArduinoJson.h>

extern String ident;

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
      DVL_PRINTLN("TRAMAAAAAAAA RECIBIDAAAAAAAAAAAAAA");
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
        DVL_PRINT("endmaker");
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
      DVL_PRINTLN(">> Trama recibida:");
      for (int i = 0; i < count; i++) {
        DVL_PRINT("S");
        DVL_PRINT(i);
        DVL_PRINT(": ");
        DVL_PRINTLN(sensorValues[i]);
      }

      // Limpio para la proxima trama
      DVL_PRINT("<< Respuesta de expansora: ");
      DVL_PRINTLN(buffer);
      buffer = "";
    }
  }
}

void imprimirSensorValuesValidos() {
  DVL_PRINTLN("ultimos sensores validos recibidos:");
  for (int i = 0; i < 16; i++) {
    if (sensorValues[i].length() > 0 && sensorValues[i] != "nan") {
      DVL_PRINT("S ");
      DVL_PRINT(i);
      DVL_PRINT(": ");
      DVL_PRINTLN(sensorValues[i]);
    }
  }
}

void leerYRetransmitirSerial(Stream &serial) {
  static String jsonBuffer = "";
  static unsigned long lastRecvTime = 0;

  if (jsonBuffer.length() > 0 && millis() - lastRecvTime > 5000) {
    DVL_PRINT("[SERIAL] Timeout de buffer de retransmisión. Descartado: ");
    DVL_PRINTLN(jsonBuffer);
    jsonBuffer = "";
  }

  while (serial.available()) {
    char c = serial.read();
    lastRecvTime = millis();

    if (c == '\n') {
      jsonBuffer.trim();
      if (jsonBuffer.length() > 0) {
        DVL_PRINTLN("[SERIAL] JSON completo recibido. Retransmitiendo...");
        
        // Parsear para extraer el tópico si es un JSON
        StaticJsonDocument<2048> doc;
        String sendTopic = "";
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        if (!error && doc.containsKey("topic")) {
          sendTopic = doc["topic"].as<String>();
        } else {
          // Si no tiene topic o no es JSON válido, usamos un tópico por defecto
          sendTopic = "DVL/LILY-GO/" + ident + "/RETRANSMITIDO";
          sendTopic.toUpperCase();
        }

        if (mqtt.connected()) {
          publish_mqtt_json(sendTopic, jsonBuffer);
        } else {
          flash_save_packet(jsonBuffer.c_str());
          DVL_PRINTLN("[SERIAL] MQTT desconectado. Datos guardados en flash.");
        }
      }
      jsonBuffer = "";
    } else if (c != '\r') {
      // Evitamos desbordamiento de memoria por ruido en la línea serial
      if (jsonBuffer.length() < 2048) {
        jsonBuffer += c;
      } else {
        DVL_PRINTLN("[SERIAL] ERROR: Buffer de retransmisión excedió los 2048 bytes. Descartando...");
        jsonBuffer = "";
      }
    }
  }
}
