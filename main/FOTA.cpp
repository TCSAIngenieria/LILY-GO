#include "FOTA.h"
#include "Debug.h"
#include <WiFi.h>              // Necesario para WiFi.status() y WL_CONNECTED
#include <WiFiClientSecure.h>  // Para conexion HTTPS y client.setInsecure()
#include <Update.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "GPRS.h"
#include <Preferences.h>

FOTAClass FOTA;
static bool updateStarted = false;  // Para evitar repetir la actualizacion

extern TinyGsm modem;
extern uint en_modem;
extern Preferences preferences;

bool parsearURL(String url, String &protocol, String &host, int &port, String &path) {
  url.trim();
  int protoEnd = url.indexOf("://");
  if (protoEnd == -1) return false;
  
  protocol = url.substring(0, protoEnd);
  String rest = url.substring(protoEnd + 3);
  int slashIdx = rest.indexOf('/');
  if (slashIdx == -1) {
    host = rest;
    path = "/";
  } else {
    host = rest.substring(0, slashIdx);
    path = rest.substring(slashIdx);
  }
  
  int colonIdx = host.indexOf(':');
  if (colonIdx != -1) {
    port = host.substring(colonIdx + 1).toInt();
    host = host.substring(0, colonIdx);
  } else {
    port = (protocol.equalsIgnoreCase("https")) ? 443 : 80;
  }
  return true;
}

static bool realizarFOTAGenerico(Client* netClient, const String& host, int port, const String& path, unsigned long timeoutSegundos) {
  DVL_PRINTLN("[FOTA] Conectando a " + host + ":" + String(port));
  
  netClient->setTimeout(15000); // 15 segundos para operaciones de socket
  
  if (!netClient->connect(host.c_str(), port)) {
    DVL_PRINTLN("[FOTA] ERROR: No se pudo conectar al servidor.");
    return false;
  }

  DVL_PRINTLN("[FOTA] Enviando peticion HTTP GET...");
  netClient->print(String("GET ") + path + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");

  // Leer headers
  int contentLength = 0;
  bool headerEnded = false;
  unsigned long startTime = millis();
  unsigned long timeoutMs = timeoutSegundos * 1000;

  while (netClient->connected() && !headerEnded) {
    if (millis() - startTime > 15000) {
      DVL_PRINTLN("[FOTA] Timeout esperando fin de cabeceras.");
      netClient->stop();
      return false;
    }

    if (netClient->available()) {
      String line = netClient->readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        headerEnded = true;
      } else if (line.startsWith("Content-Length:")) {
        contentLength = line.substring(15).toInt();
      }
    } else {
      delay(10);
    }
  }

  if (contentLength <= 0) {
    DVL_PRINTLN("[FOTA] ERROR: Content-Length invalido o no recibido.");
    netClient->stop();
    return false;
  }

  DVL_PRINTF("[FOTA] Tamaño del firmware: %d bytes\n", contentLength);

  if (!Update.begin(contentLength)) {
    DVL_PRINTLN("[FOTA] ERROR: Espacio insuficiente en particion OTA.");
    netClient->stop();
    return false;
  }

  DVL_PRINTLN("[FOTA] Descargando y flasheando...");
  size_t written = 0;
  uint8_t buffer[512];
  unsigned long lastReset = millis();
  startTime = millis(); // Resetear tiempo de descarga
  unsigned long ultimoAvanceMs = millis();
  size_t ultimoWritten = 0;

  while (netClient->connected() && written < (size_t)contentLength) {
    // Control de timeout por falta de avance (10 minutos)
    if (written > ultimoWritten) {
      ultimoWritten = written;
      ultimoAvanceMs = millis();
    } else if (millis() - ultimoAvanceMs > 600000) { // 10 minutos
      DVL_PRINTLN("[FOTA] ERROR: Sin avance por mas de 10 minutos. Reiniciando equipo...");
      Update.end(false);
      netClient->stop();
      delay(1000);
      ESP.restart();
    }

    if (millis() - startTime > timeoutMs) {
      DVL_PRINTLN("[FOTA] ERROR: Timeout de descarga excedido.");
      Update.end(false);
      netClient->stop();
      return false;
    }

    int available = netClient->available();
    if (available > 0) {
      int toRead = min(available, (int)sizeof(buffer));
      int bytesRead = netClient->readBytes(buffer, toRead);
      if (bytesRead > 0) {
        int writtenBytes = Update.write(buffer, bytesRead);
        if (writtenBytes != bytesRead) {
          DVL_PRINTLN("[FOTA] ERROR: Fallo al escribir en flash.");
          Update.end(false);
          netClient->stop();
          return false;
        }
        written += writtenBytes;
        
        // Mostrar progreso
        if (millis() - lastReset > 1000) {
          float porcentaje = ((float)written / contentLength) * 100.0;
          DVL_PRINTF("[FOTA] Progreso: %.1f%% (%d/%d bytes)\n", porcentaje, (int)written, contentLength);
          lastReset = millis();
        }
      }
    } else {
      delay(5);
    }
    yield();
  }

  netClient->stop();

  if (written == (size_t)contentLength) {
    DVL_PRINTLN("[FOTA] Finalizando actualizacion...");
    if (Update.end(true)) {
      DVL_PRINTLN("[FOTA] ¡FOTA Exitoso! Reiniciando...");
      delay(1000);
      ESP.restart();
      return true;
    } else {
      DVL_PRINTF("[FOTA] Error en Update.end(): %d\n", Update.getError());
    }
  } else {
    DVL_PRINTF("[FOTA] Descarga incompleta: %d/%d bytes\n", (int)written, contentLength);
  }

  return false;
}

void FOTAClass::startUpdate(const String& url) {
  if (updateStarted) {
    DVL_PRINTLN("FOTA ya iniciada, ignorando llamada.");
    return;
  }
  updateStarted = true;

  // Desactivar watchdog
  esp_task_wdt_delete(NULL);

  DVL_PRINTLN("Iniciando actualizacion FOTA desde:");
  DVL_PRINTLN(url);

  String protocol, host, path;
  int port;
  if (!parsearURL(url, protocol, host, port, path)) {
    DVL_PRINTLN("[FOTA] ERROR: URL invalida.");
    updateStarted = false;
    esp_task_wdt_add(NULL);
    return;
  }

  // Cargar configuraciones de Preferences
  preferences.begin("fota_cfg", true);
  unsigned long timeout = preferences.getULong("timeout", 600);
  int secure = preferences.getInt("secure", 0);
  preferences.end();

  bool success = false;

  // 1. Intentar FOTA por WiFi
  if (WiFi.status() == WL_CONNECTED) {
    DVL_PRINTLN("[FOTA] Detectada conexion WiFi. Descargando...");
    if (protocol.equalsIgnoreCase("https")) {
      WiFiClientSecure wifiSecure;
      wifiSecure.setInsecure();
      success = realizarFOTAGenerico(&wifiSecure, host, port, path, timeout);
    } else {
      WiFiClient wifiNormal;
      success = realizarFOTAGenerico(&wifiNormal, host, port, path, timeout);
    }
  }
  // 2. Intentar FOTA por Modem GPRS
  else if (en_modem && modem.isGprsConnected()) {
    DVL_PRINTLN("[FOTA] Detectada conexion GPRS (Modem). Descargando...");
    if (protocol.equalsIgnoreCase("https")) {
      if (secure == 1) {
#ifdef TINY_GSM_MODEM_SIM7000SSL
        DVL_PRINTLN("[FOTA] Usando conexion segura HTTPS por modem...");
        TinyGsmClientSecure gsmSecure(modem);
        success = realizarFOTAGenerico(&gsmSecure, host, port, path, timeout);
#else
        DVL_PRINTLN("[FOTA] ERROR: HTTPS no soportado por modem en modo SIM7000 estándar.");
        success = false;
#endif
      } else {
        DVL_PRINTLN("[FOTA] GPRS: URL HTTPS pero secure=0. Forzando HTTP por puerto 80.");
        TinyGsmClient gsmNormal(modem);
        success = realizarFOTAGenerico(&gsmNormal, host, 80, path, timeout);
      }
    } else {
      TinyGsmClient gsmNormal(modem);
      success = realizarFOTAGenerico(&gsmNormal, host, port, path, timeout);
    }
  } else {
    DVL_PRINTLN("[FOTA] ERROR: No hay conexion activa de red (WiFi desconectado y GPRS desconectado).");
  }

  updateStarted = false;
  esp_task_wdt_add(NULL); // Reactivar watchdog
}
