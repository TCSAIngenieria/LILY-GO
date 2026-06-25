#include "comandos.h"  // Para acceder a getStartMarker(), getEndMarker(), getSeparator(), etc.
#include "Debug.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Fechayhora.h"
#include "FOTA.h"
#include "Comandos.h"

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
  if (!receiving && millis() - ultimaLimpiezaBuffer > intervaloLimpieza) {
    buffer = "";
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

void escucharBridgeSerial(Stream &serial) {
  static String inputBuffer = "";
  static unsigned long lastRecvChar = 0;
  
  if (inputBuffer.length() > 0 && millis() - lastRecvChar > 3000) {
    inputBuffer = ""; // Descartar por timeout de inactividad
  }
  
  while (serial.available()) {
    char c = serial.read();
    lastRecvChar = millis();
    
    if (inputBuffer.length() == 0 && c != '{') {
      continue; // Ignorar ruidos seriales
    }
    
    if (c == '\n') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, inputBuffer);
        if (!error) {
          // 1. Sincronizar hora
          if (doc.containsKey("epoch")) {
            time_t epoch = doc["epoch"].as<time_t>();
            if (epoch > 10000) {
              struct timeval tv = { .tv_sec = epoch };
              settimeofday(&tv, NULL);
              DVL_PRINTLN("[SERIAL BRIDGE] Hora sincronizada desde Gateway");
            }
          }
          
          // 2. Procesar comando
          if (doc.containsKey("cmd")) {
            String comando = doc["cmd"].as<String>();
            DVL_PRINTLN("[SERIAL BRIDGE] Comando recibido: " + comando);
            
            if (comando.startsWith("FOTA:")) {
              String url = comando.substring(5);
              DVL_PRINTLN("[SERIAL BRIDGE] Iniciando FOTA desde: " + url);
              FOTA.startUpdate(url);
            } else {
              String respuesta = procesarComando(comando);
              
              // Enviar la respuesta de vuelta al Broker
              String topicRespuesta = "DVL/NODEMCU/" + ident + "/RESPUESTA";
              topicRespuesta.toUpperCase();
              
              StaticJsonDocument<256> respDoc;
              respDoc["topic"] = topicRespuesta;
              respDoc["ident"] = ident;
              respDoc["response"] = respuesta;
              
              serializeJson(respDoc, serial);
              serial.println();
            }
          }
        } else {
          DVL_PRINT("[SERIAL BRIDGE] Error parsing JSON de respuesta: ");
          DVL_PRINTLN(inputBuffer);
        }
      }
      inputBuffer = "";
    } else if (c != '\r') {
      if (inputBuffer.length() < 512) {
        inputBuffer += c;
      } else {
        inputBuffer = ""; // Buffer overflow, descartar
      }
    }
  }
}
