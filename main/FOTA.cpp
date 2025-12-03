#include "FOTA.h"
#include <WiFi.h>              // Necesario para WiFi.status() y WL_CONNECTED
#include <WiFiClientSecure.h>  // Para conexion HTTPS y client.setInsecure()
#include <HTTPClient.h>
#include <Update.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

FOTAClass FOTA;
WiFiClientSecure client;

static bool updateStarted = false;  // Para evitar repetir la actualizacion

void FOTAClass::startUpdate(const String& url) {
  if (updateStarted) {
    Serial.println("FOTA ya iniciada, ignorando llamada.");
    return;
  }
  updateStarted = true;

  // Desactivo watchdog para esta tarea antes de empezar FOTA
  esp_task_wdt_delete(NULL);

  Serial.println("Iniciando actualizacion FOTA desde:");
  Serial.println(url);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No hay conexion WiFi. No se puede actualizar.");
    updateStarted = false;
    esp_task_wdt_add(NULL);  // Reactivo watchdog
    return;
  }

  HTTPClient http;
  Serial.println("Conectando al servidor...");
  client.setInsecure();  // Desactiva validacion SSL (usar solo para pruebas)
  http.begin(client, url);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf(" Error HTTP al bajar el binario: %d\n", httpCode);
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Tamaño del binario invalido.");
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.println("No se pudo iniciar la actualizacion. Memoria insuficiente?");
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  Serial.println("Comenzando descarga y actualizacion...");
  WiFiClient* stream = http.getStreamPtr();

  size_t written = 0;
  const size_t bufferSize = 512;
  uint8_t buff[bufferSize];

  while (http.connected() && written < (size_t)contentLength) {
    size_t available = stream->available();
    if (available) {
      size_t toRead = (available > bufferSize) ? bufferSize : available;
      int readBytes = stream->readBytes(buff, toRead);
      if (readBytes <= 0) {
        Serial.println(" Error leyendo los datos del stream.");
        http.end();
        updateStarted = false;
        esp_task_wdt_add(NULL);
        return;
      }

      int writtenBytes = Update.write(buff, readBytes);
      if (writtenBytes != readBytes) {
        Serial.println(" Error escribiendo la actualizacion.");
        http.end();
        updateStarted = false;
        esp_task_wdt_add(NULL);
        return;
      }

      written += writtenBytes;

      esp_task_wdt_reset();  // Resetea el watchdog
      yield();
      delay(1);

      Serial.printf("Descargado %d/%d bytes\n", (int)written, contentLength);
    } else {
      delay(1);
    }
  }

  http.end();

  if (written == (size_t)contentLength) {
    Serial.println("Finalizando actualizacion...");
    if (Update.end(true)) {
      Serial.println("Actualizacion exitosa, reiniciando...");
      ESP.restart();
    } else {
      Serial.printf("Error en Update.end(): %d\n", Update.getError());
      updateStarted = false;
      esp_task_wdt_add(NULL);
    }
  } else {
    Serial.printf("Descarga incompleta: %d de %d bytes\n", (int)written, contentLength);
    updateStarted = false;
    esp_task_wdt_add(NULL);
  }
}
