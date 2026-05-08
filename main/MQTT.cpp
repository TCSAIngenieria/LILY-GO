#include "MQTT.h"
#include "Debug.h"
#include "Comandos.h"
#include "FOTA.h"
#include "esp_task_wdt.h"
#include <ArduinoJson.h>
#include <Preferences.h>

#define LED_PIN 12

extern PubSubClient mqtt;
extern FOTAClass FOTA;

extern String ident;
extern String versionado;
extern Preferences preferences;

extern bool mqttActivo;
extern unsigned long mqttUltimaConexionOK;

String MQTT_BROKER = "192.168.7.252"; // Valor por defecto
int MQTT_PORT = 7183;                 // Valor por defecto

// Auxiliar para reducir duplicación de código y ahorrar IRAM
static String finalizeJson(JsonDocument& doc) {
  char payload[1024]; 
  serializeJson(doc, payload, sizeof(payload));
  return String(payload);
}

boolean mqttConnect() {
  DVL_PRINT("Conectando a MQTT broker: ");
  DVL_PRINTLN(MQTT_BROKER);

  if (mqtt.connect(ident.c_str())) {
    DVL_PRINTLN(" Conectado a MQTT!");
    mqttActivo = true;
    mqttUltimaConexionOK = millis();

    String topicFOTA = "DVL/LILY-GO/" + ident + "/FOTA";
    String topicCMD = "DVL/LILY-GO/" + ident + "/COMANDOS";

    mqtt.subscribe(topicFOTA.c_str());
    mqtt.subscribe(topicCMD.c_str());

    DVL_PRINTLN("📡 Suscripto a topics FOTA y COMANDOS");
    esp_task_wdt_reset();
    return true;
  } else {
    DVL_PRINT(" Error al conectar a MQTT. Codigo: ");
    DVL_PRINTLN(mqtt.state());
    esp_task_wdt_reset();
    return false;
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int len) {
  String message;
  for (unsigned int i = 0; i < len; i++) {
    message += (char)payload[i];
  }
  message.trim();

  String topicStr = String(topic);

  // FOTA
  if (topicStr.endsWith("/FOTA")) {
    preferences.begin("fota", true);
    String storedURL = preferences.getString("url", "");
    preferences.end();

    if (storedURL != message) {
      DVL_PRINTLN(" Nueva URL FOTA detectada. Iniciando actualizacion...");
      FOTA.startUpdate(message);
    } else {
      DVL_PRINTLN("URL FOTA igual a la actual. Ignorando.");
    }
  }

  // COMANDOS
  else if (topicStr.endsWith("/COMANDOS")) {
    DVL_PRINTLN(" Comando MQTT recibido: " + message);
    String topicRespuesta = "DVL/LILY-GO/" + ident + "/RESPUESTA";
    String respuesta = procesarComando(message);
    if (mqtt.connected()) {
      mqtt.publish(topicRespuesta.c_str(), respuesta.c_str());
    }
  }
}

bool publish_mqtt_json(String topic, String jsonPayload) {
  if (!mqtt.connected()) {
    DVL_PRINTLN("MQTT no conectado, no se puede publicar.");
    return false;
  }

  bool sent = mqtt.publish(topic.c_str(), jsonPayload.c_str());
  DVL_PRINT("Publicado JSON en topic ");
  DVL_PRINT(topic);
  DVL_PRINT(": ");
  DVL_PRINTLN(sent ? "OK" : "FALLo");
  DVL_PRINTLN(jsonPayload); 

  return sent;
}

String create_mqtt_json_adc(String ident, String fechayhora, float adc0,
                            float adc1, float adc2) {
  StaticJsonDocument<256> doc;
  doc["ident"] = ident;
  doc["status"] = "adc-values";
  doc["date"] = fechayhora;
  doc["adc0"] = adc0;
  doc["adc1"] = adc1;
  doc["adc2"] = adc2;
 
  return finalizeJson(doc);
}

String create_mqtt_json_pivot(String ident, String fechayhora, int di1,
                              int di2, int sirena, unsigned long index) {
  StaticJsonDocument<256> doc;
  doc["Ident"] = ident;
  doc["Date"] = fechayhora;
  doc["DI1"] = di1;
  doc["DI2"] = di2;
  doc["Sirena"] = sirena;
  doc["index"] = index;
  
  return finalizeJson(doc);
}

String create_mqtt_json_keepalive(String ident, String fechayhora,
                                  String latitud, String longitud,
                                  String versionado, unsigned long rebootCount,
                                  String connType, String connDetail,
                                  String imei, String imsi, String iccid,
                                  String rsrq, String rsrp, String rssi) {
  StaticJsonDocument<512> doc;
  doc["ident"] = ident;
  doc["status"] = "keep-alive";
  doc["date"] = fechayhora;
  doc["latitud"] = latitud;
  doc["longitud"] = longitud;
  doc["Version"] = versionado;
  doc["reboot_count"] = rebootCount;
  doc["conn_type"] = connType;
  doc["conn_detail"] = connDetail;
  doc["IMEI"] = imei;
  doc["IMSI"] = imsi;
  doc["ICCID"] = iccid;
  doc["RSRQ"] = rsrq;
  doc["RSRP"] = rsrp;
  doc["RSSI"] = rssi;
 
  return finalizeJson(doc);
}
