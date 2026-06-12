#include "BridgeAP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "MQTT.h"
#include "Flash.h"
#include "Debug.h"

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
      
      if (mqtt.connected()) {
        publish_mqtt_json(topic, payload);
        bridgeServer.send(200, "text/plain", "OK");
      } else {
        flash_save_packet(payload.c_str());
        DVL_PRINTLN("[BRIDGE] MQTT desconectado. Guardado en flash.");
        bridgeServer.send(200, "text/plain", "SAVED_OFFLINE");
      }
    } else {
      DVL_PRINTLN("[BRIDGE] JSON inválido o sin campo 'topic'");
      bridgeServer.send(400, "text/plain", "INVALID_JSON");
    }
  } else {
    bridgeServer.send(400, "text/plain", "NO_BODY");
  }
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
  bridgeServer.begin();
  DVL_PRINTLN("[BRIDGE] Servidor HTTP de retransmisión iniciado en puerto 8080");
}

void mantenerBridgeAP() {
  bridgeServer.handleClient();
}
