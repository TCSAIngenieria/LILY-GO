#include "FOTA.h"
#include "Debug.h"
#include <WiFi.h>              // Necesario para WiFi.status() y WL_CONNECTED
#include <WiFiClientSecure.h>  // Para conexion HTTPS y client.setInsecure()
#include <HTTPClient.h>
#include <Update.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

FOTAClass FOTA;

static bool updateStarted = false;  // Para evitar repetir la actualizacion

void FOTAClass::startUpdate(const String& url) {
  if (updateStarted) {
    DVL_PRINTLN("FOTA ya iniciada, ignorando llamada.");
    return;
  }
  updateStarted = true;

  // Desactivo watchdog para esta tarea antes de empezar FOTA
  esp_task_wdt_delete(NULL);

  DVL_PRINTLN("Iniciando actualizacion FOTA desde:");
  DVL_PRINTLN(url);

  if (WiFi.status() != WL_CONNECTED) {
    DVL_PRINTLN("No hay conexion WiFi. No se puede actualizar.");
    updateStarted = false;
    esp_task_wdt_add(NULL);  // Reactivo watchdog
    return;
  }

  HTTPClient http;
  http.setTimeout(30000); // 30 segundos para dar tiempo al proxy GPRS
  WiFiClient normalClient;
  WiFiClientSecure secureClient;

  DVL_PRINTLN("Conectando al servidor...");
  if (url.startsWith("https://")) {
    secureClient.setInsecure();  // Desactiva validacion SSL
    http.begin(secureClient, url);
  } else {
    http.begin(normalClient, url);
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    DVL_PRINTF(" Error HTTP al bajar el binario: %d\n", httpCode);
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    DVL_PRINTLN("Tamaño del binario invalido.");
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DVL_PRINTLN("No se pudo iniciar la actualizacion. Memoria insuficiente?");
    http.end();
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  DVL_PRINTLN("Comenzando descarga y actualizacion...");
  WiFiClient* stream = http.getStreamPtr();

  size_t written = 0;
  const size_t bufferSize = 512;
  uint8_t buff[bufferSize];
  int ultimoPorcentajeX10 = -1;
  unsigned long ultimoAvanceMs = millis();
  size_t ultimoWritten = 0;

  while (http.connected() && written < (size_t)contentLength) {
    // Control de timeout por falta de avance (10 minutos)
    if (written > ultimoWritten) {
      ultimoWritten = written;
      ultimoAvanceMs = millis();
    } else if (millis() - ultimoAvanceMs > 600000) { // 10 minutos
      DVL_PRINTLN(" Error: FOTA sin avance por mas de 10 minutos. Reiniciando equipo...");
      delay(1000);
      ESP.restart();
    }

    size_t available = stream->available();
    if (available) {
      size_t toRead = (available > bufferSize) ? bufferSize : available;
      int readBytes = stream->readBytes(buff, toRead);
      if (readBytes <= 0) {
        DVL_PRINTLN(" Error leyendo los datos del stream.");
        http.end();
        updateStarted = false;
        esp_task_wdt_add(NULL);
        return;
      }

      int writtenBytes = Update.write(buff, readBytes);
      if (writtenBytes != readBytes) {
        DVL_PRINTLN(" Error escribiendo la actualizacion.");
        http.end();
        updateStarted = false;
        esp_task_wdt_add(NULL);
        return;
      }

      written += writtenBytes;

      yield();
      delay(1);

      int porcentajeX10 = (written * 1000) / contentLength;
      if (porcentajeX10 != ultimoPorcentajeX10) {
        DVL_PRINTF("Descargando: %.1f%%\n", (float)porcentajeX10 / 10.0f);
        ultimoPorcentajeX10 = porcentajeX10;
      }
    } else {
      delay(1);
    }
  }

  http.end();

  if (written == (size_t)contentLength) {
    DVL_PRINTLN("Finalizando actualizacion...");
    if (Update.end(true)) {
      DVL_PRINTLN("Actualizacion exitosa, reiniciando...");
      ESP.restart();
    } else {
      DVL_PRINTF("Error en Update.end(): %d\n", Update.getError());
      updateStarted = false;
      esp_task_wdt_add(NULL);
    }
  } else {
    DVL_PRINTF("Descarga incompleta: %d de %d bytes\n", (int)written, contentLength);
    updateStarted = false;
    esp_task_wdt_add(NULL);
  }
}
