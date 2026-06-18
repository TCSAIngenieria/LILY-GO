#include "BridgeAP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "MQTT.h"
#include "Flash.h"
#include "Debug.h"

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

