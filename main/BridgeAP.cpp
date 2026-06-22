#include "BridgeAP.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "MQTT.h"
#include "Flash.h"
#include "Debug.h"
#include "FOTA.h"
#include "GPRS.h"
#include "esp_task_wdt.h"
#include <Preferences.h>

extern TinyGsm modem;
extern uint en_modem;
extern Preferences preferences;

String sateliteFotaUrl = "";

#define MAX_PENDING_CMDS 20

struct ComandoPendiente {
  String ident;
  String mensaje;
  bool enUso = false;
};

ComandoPendiente colaComandos[MAX_PENDING_CMDS];

bool agregarComandoCola(String ident, String mensaje) {
  for (int i = 0; i < MAX_PENDING_CMDS; i++) {
    if (!colaComandos[i].enUso) {
      colaComandos[i].ident = ident;
      colaComandos[i].mensaje = mensaje;
      colaComandos[i].enUso = true;
      DVL_PRINTLN("[BRIDGE] Comando encolado para " + ident + ": " + mensaje);
      return true;
    }
  }
  DVL_PRINTLN("[BRIDGE] ERROR: Cola de comandos llena!");
  return false;
}

String obtenerComandoCola(String ident) {
  for (int i = 0; i < MAX_PENDING_CMDS; i++) {
    if (colaComandos[i].enUso && colaComandos[i].ident.equalsIgnoreCase(ident)) {
      String mensaje = colaComandos[i].mensaje;
      colaComandos[i].enUso = false;
      colaComandos[i].ident = "";
      colaComandos[i].mensaje = "";
      return mensaje;
    }
  }
  return "";
}

WebServer bridgeServer(8080);

void handleRetransmit() {
  if (bridgeServer.hasArg("plain")) {
    String payload = bridgeServer.arg("plain");
    DVL_PRINTLN("[BRIDGE] JSON recibido por HTTP POST:");
    DVL_PRINTLN(payload);

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error && doc.containsKey("topic")) {
      String topic = doc["topic"].as<String>();
      String senderIdent = "";
      if (doc.containsKey("ident")) {
        senderIdent = doc["ident"].as<String>();
      }
      
      bool esRespuesta = false;
      if (topic.endsWith("/RESPUESTA") && doc.containsKey("response")) {
        esRespuesta = true;
      }
      
      if (mqtt.connected()) {
        if (esRespuesta) {
          String rawResponse = doc["response"].as<String>();
          mqtt.publish(topic.c_str(), rawResponse.c_str());
          DVL_PRINTLN("[BRIDGE] Respuesta de satelite retransmitida cruda a MQTT");
        } else {
          publish_mqtt_json(topic, payload);
        }
        
        String cmdPendiente = "";
        if (senderIdent.length() > 0) {
          cmdPendiente = obtenerComandoCola(senderIdent);
        }
        
        if (cmdPendiente.length() > 0) {
          bridgeServer.send(200, "text/plain", "OK|CMD:" + cmdPendiente);
          DVL_PRINTLN("[BRIDGE] Retransmision OK. Enviado comando pendiente: " + cmdPendiente);
        } else {
          bridgeServer.send(200, "text/plain", "OK");
        }
      } else {
        if (!esRespuesta) {
          flash_save_packet(payload.c_str());
          DVL_PRINTLN("[BRIDGE] MQTT desconectado. Guardado en flash.");
        }
        
        String cmdPendiente = "";
        if (senderIdent.length() > 0) {
          cmdPendiente = obtenerComandoCola(senderIdent);
        }
        
        if (cmdPendiente.length() > 0) {
          bridgeServer.send(200, "text/plain", "SAVED_OFFLINE|CMD:" + cmdPendiente);
        } else {
          bridgeServer.send(200, "text/plain", "SAVED_OFFLINE");
        }
      }
    } else {
      DVL_PRINTLN("[BRIDGE] JSON inválido o sin campo 'topic'");
      bridgeServer.send(400, "text/plain", "INVALID_JSON");
    }
  } else {
    bridgeServer.send(400, "text/plain", "NO_BODY");
  }
}

void handleFotaProxy() {
  DVL_PRINTLN("[BRIDGE PROXY] Solicitud de FOTA proxy recibida del satelite...");
  if (sateliteFotaUrl.length() == 0) {
    DVL_PRINTLN("[BRIDGE PROXY] ERROR: No hay URL configurada en el Gateway.");
    bridgeServer.send(400, "text/plain", "NO_FOTA_CONFIGURED");
    return;
  }

  String protocol, host, path;
  int port;
  if (!parsearURL(sateliteFotaUrl, protocol, host, port, path)) {
    DVL_PRINTLN("[BRIDGE PROXY] ERROR: URL de FOTA invalida en el Gateway.");
    bridgeServer.send(400, "text/plain", "INVALID_FOTA_URL");
    return;
  }

  // Cargar configuraciones de Preferences
  preferences.begin("fota_cfg", true);
  unsigned long timeout = preferences.getULong("timeout", 600);
  int secure = preferences.getInt("secure", 0);
  preferences.end();

  Client* netClient = nullptr;
  WiFiClientSecure wifiSecure;
  WiFiClient wifiNormal;
  TinyGsmClient gsmNormal(modem);
#ifdef TINY_GSM_MODEM_SIM7000SSL
  TinyGsmClientSecure gsmSecure(modem);
#endif

  // 1. Determinar el cliente de red a usar según la interfaz activa
  if (WiFi.status() == WL_CONNECTED) {
    DVL_PRINTLN("[BRIDGE PROXY] Conectando via WiFi...");
    if (protocol.equalsIgnoreCase("https")) {
      wifiSecure.setInsecure();
      netClient = &wifiSecure;
    } else {
      netClient = &wifiNormal;
    }
  } else if (en_modem && modem.isGprsConnected()) {
    DVL_PRINTLN("[BRIDGE PROXY] Conectando via GPRS (Modem)...");
    if (protocol.equalsIgnoreCase("https")) {
      if (secure == 1) {
#ifdef TINY_GSM_MODEM_SIM7000SSL
        DVL_PRINTLN("[BRIDGE PROXY] Usando conexion segura HTTPS por modem...");
        netClient = &gsmSecure;
#else
        DVL_PRINTLN("[BRIDGE PROXY] ERROR: HTTPS no soportado por modem en modo SIM7000 estándar.");
        bridgeServer.send(503, "text/plain", "HTTPS_NOT_SUPPORTED");
        return;
#endif
      } else {
        DVL_PRINTLN("[BRIDGE PROXY] GPRS: URL HTTPS pero secure=0. Forzando HTTP por puerto 80.");
        port = 80;
        netClient = &gsmNormal;
      }
    } else {
      netClient = &gsmNormal;
    }
  }

  if (netClient == nullptr) {
    DVL_PRINTLN("[BRIDGE PROXY] ERROR: Sin interfaz de red activa.");
    bridgeServer.send(503, "text/plain", "NO_INTERNET_CONNECTION");
    return;
  }

  netClient->setTimeout(15000); // 15 segundos timeout de socket
  if (!netClient->connect(host.c_str(), port)) {
    DVL_PRINTLN("[BRIDGE PROXY] ERROR: No se pudo conectar al servidor de firmware.");
    bridgeServer.send(502, "text/plain", "CONNECTION_FAILED");
    return;
  }

  DVL_PRINTLN("[BRIDGE PROXY] Enviando GET al servidor externo...");
  netClient->print(String("GET ") + path + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");

  // Leer cabeceras
  int contentLength = 0;
  bool headerEnded = false;
  unsigned long startTime = millis();

  while (netClient->connected() && !headerEnded) {
    if (millis() - startTime > 15000) {
      DVL_PRINTLN("[BRIDGE PROXY] Timeout leyendo cabeceras del servidor externo.");
      netClient->stop();
      bridgeServer.send(504, "text/plain", "TIMEOUT_HEADERS");
      return;
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
    DVL_PRINTLN("[BRIDGE PROXY] ERROR: Content-Length invalido o no recibido del servidor.");
    netClient->stop();
    bridgeServer.send(502, "text/plain", "INVALID_CONTENT_LENGTH");
    return;
  }

  DVL_PRINTF("[BRIDGE PROXY] Retransmitiendo firmware de %d bytes al satelite...\n", contentLength);

  // Enviar cabeceras HTTP de respuesta al satelite
  bridgeServer.setContentLength(contentLength);
  bridgeServer.send(200, "application/octet-stream", "");

  // Stream proxy de bytes en tiempo real
  WiFiClient localClient = bridgeServer.client();
  size_t retransmitted = 0;
  uint8_t buffer[512];
  unsigned long lastReset = millis();
  startTime = millis();
  unsigned long timeoutMs = timeout * 1000;

  while (netClient->connected() && localClient.connected() && retransmitted < (size_t)contentLength) {
    if (millis() - startTime > timeoutMs) {
      DVL_PRINTLN("[BRIDGE PROXY] Timeout de descarga excedido.");
      break;
    }

    int available = netClient->available();
    if (available > 0) {
      int toRead = min(available, (int)sizeof(buffer));
      int bytesRead = netClient->readBytes(buffer, toRead);
      if (bytesRead > 0) {
        localClient.write(buffer, bytesRead);
        retransmitted += bytesRead;
        
        if (millis() - lastReset > 1000) {
          float porcentaje = ((float)retransmitted / contentLength) * 100.0;
          DVL_PRINTF("[BRIDGE PROXY] Retransmitidos: %.1f%% (%d/%d bytes)\n", porcentaje, (int)retransmitted, contentLength);
          esp_task_wdt_reset();
          lastReset = millis();
        }
      }
    } else {
      delay(5);
    }
    yield();
  }

  netClient->stop();
  DVL_PRINTLN("[BRIDGE PROXY] Finalizada retransmision de streaming.");
  
  // Limpiar URL para evitar reuso accidental
  sateliteFotaUrl = "";

  DVL_PRINTLN("[BRIDGE PROXY] Reiniciando LilyGo para reinicializar todos los procesos...");
  delay(2000);
  ESP.restart();
}

void iniciarBridgeAP() {
  DVL_PRINTLN("Iniciando AP Bridge...");
  
  WiFi.mode(WIFI_AP);

  // softAP(ssid, passphrase, channel, hidden, max_connection)
  // ssid: "LILYGO_BRIDGE_NET"
  // passphrase: "TCSA-Bridge-2026"
  // channel: 1
  // hidden: 1 (oculto)
  // max_connection: 10 (límite máximo solicitado por el usuario)
  WiFi.softAP("LILYGO_BRIDGE_NET", "TCSA-Bridge-2026", 1, 1, 10);

  DVL_PRINT("AP IP: ");
  DVL_PRINTLN(WiFi.softAPIP());

  bridgeServer.on("/retransmit", HTTP_POST, handleRetransmit);
  bridgeServer.on("/fota_proxy", HTTP_GET, handleFotaProxy);
  bridgeServer.begin();
  DVL_PRINTLN("[BRIDGE] Servidor HTTP de retransmisión iniciado en puerto 8080");
}

void mantenerBridgeAP() {
  bridgeServer.handleClient();
}

