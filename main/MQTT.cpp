#include "MQTT.h"
#include "Comandos.h"
#include "FOTA.h"
#include "Modbus.h"
#include "esp_task_wdt.h"
#include <ArduinoJson.h>
#include <Preferences.h>

#define LED_PIN 12
const char *topicInit = "LilyGo/topicInit";
const char *latitud = "LilyGo/LAT";
const char *longitud = "LilyGo/LONG";

extern PubSubClient mqtt;
extern FOTAClass FOTA;

extern String ident;
extern String versionado;
extern Preferences preferences;

extern bool mqttActivo;
extern unsigned long mqttUltimaConexionOK;

extern String modbus_lastValues[MODBUS_MAX_FRAMES];

String MQTT_BROKER = "192.168.7.252"; // Valor por defecto
int MQTT_PORT = 7183;                 // Valor por defecto

boolean mqttConnect() {
  Serial.print("Conectando a MQTT broker: ");
  Serial.println(MQTT_BROKER);

  if (mqtt.connect(ident.c_str())) {
    Serial.println(" Conectado a MQTT!");
    mqttActivo = true;
    mqttUltimaConexionOK = millis();

    String topicFOTA = "DVL/LILY-GO/" + ident + "/FOTA";
    String topicCMD = "DVL/LILY-GO/" + ident + "/COMANDOS";

    mqtt.subscribe(topicFOTA.c_str());
    mqtt.subscribe(topicCMD.c_str());

    Serial.println("📡 Suscripto a topics FOTA y COMANDOS");
    esp_task_wdt_reset();
    return true;
  } else {
    Serial.print(" Error al conectar a MQTT. Codigo: ");
    Serial.println(mqtt.state());
    esp_task_wdt_reset();
    return false;
  }
}

// Or, if you want to authenticate MQTT:
// boolean status = mqtt.connect(MQTT_ID, MQTT_USER, MQTT_PASSWORD);

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
      Serial.println(" Nueva URL FOTA detectada. Iniciando actualizacion...");
      FOTA.startUpdate(message);
    } else {
      Serial.println("URL FOTA igual a la actual. Ignorando.");
    }
  }

  // COMANDOS
  else if (topicStr.endsWith("/COMANDOS")) {
    Serial.println(" Comando MQTT recibido: " + message);

    // Construir topic de respuesta
    String topicRespuesta = "DVL/LILY-GO/" + ident + "/RESPUESTA";

    String respuesta = procesarComando(message);
    // Publicar la respuesta en el topico
    if (mqtt.connected()) {
      mqtt.publish(topicRespuesta.c_str(), respuesta.c_str());
    }
  }

  else {
    Serial.println(" Topico MQTT no manejado: " + topicStr);
  }
}

bool publish_mqtt_json(String topic, String jsonPayload) {
  if (!mqtt.connected()) {
    Serial.println("MQTT no conectado, no se puede publicar.");
    return false;
  }

  bool sent = mqtt.publish(topic.c_str(), jsonPayload.c_str());
  Serial.print("Publicado JSON en topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(sent ? "OK" : "FALLo");
  Serial.println(jsonPayload); // Para debug: imprime el JSON publicado

  return sent;
}

String create_mqtt_json_sensor(String topic, String ident,
                               String valor_variable, String fechayhora,
                               float Vbateria,
                               float Vprincipal, unsigned long numeroPaquete) {
  StaticJsonDocument<256> doc;
  doc["ident"] = ident;
  doc["temperatura"] = valor_variable;
  doc["date"] = fechayhora;
  doc["Tension_bateria"] = Vbateria;
  doc["Tension_principal"] = Vprincipal;
  doc["Version"] = versionado;
  doc["index"] = numeroPaquete;

  char payload[256];
  serializeJson(doc, payload);
  return String(payload);
}

String create_mqtt_json_serial(String topic, String ident, String S0, String S1,
                               String S2, String S3, String S4, String S5,
                               String S6, String S7, String S8, String S9,
                               String S10, String S11, String S12, String S13,
                               String S14, String S15, String fechayhora,
                               String latitud, String longitud, float Vbateria,
                               float Vprincipal, unsigned long numeroPaquete) {
  StaticJsonDocument<512> doc;
  doc["ident"] = ident;
  doc["S0"] = S0;
  doc["S1"] = S1;
  doc["S2"] = S2;
  doc["S3"] = S3;
  doc["S4"] = S4;
  doc["S5"] = S5;
  doc["S6"] = S6;
  doc["S7"] = S7;
  doc["S8"] = S8;
  doc["S9"] = S9;
  doc["S10"] = S10;
  doc["S11"] = S11;
  doc["S12"] = S12;
  doc["S13"] = S13;
  doc["S14"] = S14;
  doc["S15"] = S15;
  doc["date"] = fechayhora;
  doc["latitud"] = latitud;
  doc["longitud"] = longitud;
  doc["Tension_bateria"] = Vbateria;
  doc["Tension_principal"] = Vprincipal;
  doc["Version"] = versionado;
  doc["index"] = numeroPaquete;

  char payload[512];
  serializeJson(doc, payload);
  return String(payload);
}

String create_mqtt_json_modbus(String topic, String ident, String fechayhora,
                               String latitud, String longitud, float Vbateria,
                               float Vprincipal, unsigned long numeroPaquete) {
  StaticJsonDocument<512> doc;
  doc["ident"] = ident;

  // Recorrer las consultas configuradas
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    if (modbus_lastValues[i].length() > 0) {
      String key = "INDEX" + String(i + 1);
      doc[key] = modbus_lastValues[i];
    }
  }

  doc["date"] = fechayhora;
  doc["latitud"] = latitud;
  doc["longitud"] = longitud;
  doc["Tension_bateria"] = Vbateria;
  doc["Tension_principal"] = Vprincipal;
  doc["Version"] = versionado;
  doc["index"] = numeroPaquete;

  char payload[512];
  serializeJson(doc, payload);
  return String(payload);
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

  char payload[256];
  serializeJson(doc, payload);
  return String(payload);
}

String create_mqtt_json_keepalive(String ident, String fechayhora,
                                  String latitud, String longitud,
                                  String versionado,
                                  unsigned long rebootCount) {
  StaticJsonDocument<256> doc;
  doc["ident"] = ident;
  doc["status"] = "keep-alive";
  doc["date"] = fechayhora;
  doc["latitud"] = latitud;
  doc["longitud"] = longitud;
  doc["Version"] = versionado;
  doc["reboot_count"] = rebootCount;

  char payload[256];
  serializeJson(doc, payload);
  return String(payload);
}

String create_mqtt_json_ble(String topic, String ident, String fechayhora,
                            String name, float temp, float hum,
                            int batteryLevel, float accelX, float accelY,
                            float accelZ, String tag_id, String uuid,
                            String latitud, String longitud, float Vbateria,
                            float Vprincipal, unsigned long numeroPaquete,
                            int motion, int door) {
  StaticJsonDocument<512> doc;
  doc["name"] = name;
  doc["tag_id"] = tag_id;
  doc["UUID"] = uuid;
  doc["date"] = fechayhora;
  doc["temp"] = String(temp, 2);
  doc["hum"] = String(hum, 2);
  doc["mov"] = motion;
  doc["door"] = door;
  doc["% bat"] = batteryLevel;
  doc["Accel_X"] = accelX;
  doc["Accel_Y"] = accelY;
  doc["Accel_Z"] = accelZ;
  doc["index"] = numeroPaquete;

  char payload[512];
  serializeJson(doc, payload);
  return String(payload);
}
