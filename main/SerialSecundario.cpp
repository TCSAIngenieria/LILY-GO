#include "comandos.h"  // Para acceder a getStartMarker(), getEndMarker(), getSeparator(), etc.
#include "Debug.h"
#include <Arduino.h>
#include "MQTT.h"
#include "Flash.h"
#include <ArduinoJson.h>
#include "BridgeAP.h"
#include <time.h>

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

    // Si el buffer está vacío, ignoramos cualquier carácter hasta encontrar el inicio de un JSON '{'
    if (jsonBuffer.length() == 0 && c != '{') {
      continue;
    }

    if (c == '\n') {
      jsonBuffer.trim();
      if (jsonBuffer.length() > 0) {
        // Parsear para extraer el tópico y validar el JSON
        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        if (!error && doc.containsKey("topic")) {
          String sendTopic = doc["topic"].as<String>();
          
          bool esRespuesta = false;
          if (sendTopic.endsWith("/RESPUESTA") && doc.containsKey("response")) {
            esRespuesta = true;
          }

          if (mqtt.connected()) {
            if (esRespuesta) {
              String rawResponse = doc["response"].as<String>();
              mqtt.publish(sendTopic.c_str(), rawResponse.c_str());
              DVL_PRINTLN("[SERIAL] Respuesta de satelite retransmitida cruda a MQTT");
            } else {
              publish_mqtt_json(sendTopic, jsonBuffer);
            }
          } else {
            if (!esRespuesta) {
              flash_save_packet(jsonBuffer.c_str());
              DVL_PRINTLN("[SERIAL] MQTT desconectado. Datos guardados en flash.");
            }
          }

          // Enviar respuesta por serial al satélite con la hora y comando pendiente
          time_t nowTime;
          time(&nowTime);
          
          String senderIdent = "";
          if (doc.containsKey("ident")) {
            senderIdent = doc["ident"].as<String>();
          }
          
          String cmdPendiente = "";
          if (senderIdent.length() > 0) {
            cmdPendiente = obtenerComandoCola(senderIdent);
          }
          
          StaticJsonDocument<256> respDoc;
          respDoc["epoch"] = nowTime;
          if (cmdPendiente.length() > 0) {
            respDoc["cmd"] = cmdPendiente;
          }
          
          serializeJson(respDoc, serial);
          serial.println(); // Delimitador de línea para la lectura del satélite
          
          if (cmdPendiente.length() > 0) {
            DVL_PRINTLN("[SERIAL] Retransmisión OK. Enviado comando pendiente por serial: " + cmdPendiente);
          } else {
            DVL_PRINTLN("[SERIAL] Retransmisión OK. Enviada hora actual por serial.");
          }
        } else {
          DVL_PRINT("[SERIAL] JSON inválido o sin tópico. Descartado: ");
          DVL_PRINTLN(jsonBuffer);
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
