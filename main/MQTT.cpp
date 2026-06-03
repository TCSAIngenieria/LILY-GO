#include "MQTT.h"
#include "Debug.h"
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
  DVL_PRINT("Conectando a MQTT broker: ");
  DVL_PRINTLN(MQTT_BROKER);

  if (mqtt.connect(ident.c_str())) {
    DVL_PRINTLN(" Conectado a MQTT!");
    mqttActivo = true;
    mqttUltimaConexionOK = millis();

    String topicFOTA = "DVL/NODEMCU/" + ident + "/FOTA";
    String topicCMD = "DVL/NODEMCU/" + ident + "/COMANDOS";

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
      DVL_PRINTLN(" Nueva URL FOTA detectada. Iniciando actualizacion...");
      FOTA.startUpdate(message);
    } else {
      DVL_PRINTLN("URL FOTA igual a la actual. Ignorando.");
    }
  }

  // COMANDOS
  else if (topicStr.endsWith("/COMANDOS")) {
    DVL_PRINTLN(" Comando MQTT recibido: " + message);

    // Construir topic de respuesta
    String topicRespuesta = "DVL/NODEMCU/" + ident + "/RESPUESTA";

    String respuesta = procesarComando(message);
    // Publicar la respuesta en el topico
    if (mqtt.connected()) {
      mqtt.publish(topicRespuesta.c_str(), respuesta.c_str());
    }
  }

  else {
    DVL_PRINTLN(" Topico MQTT no manejado: " + topicStr);
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
  DVL_PRINTLN(jsonPayload); // Para debug: imprime el JSON publicado

  return sent;
}

String create_mqtt_json_sensor(String topic, String ident,
                               String valor_variable, String fechayhora,
                               float Vbateria,
                               float Vprincipal, unsigned long numeroPaquete) {
  StaticJsonDocument<256> doc;
  doc["topic"] = topic;
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
  doc["topic"] = topic;
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
  doc["topic"] = topic;
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

String create_mqtt_json_adc(String topic, String ident, String fechayhora, float adc0,
                            float adc1, float adc2) {
  StaticJsonDocument<256> doc;
  doc["topic"] = topic;
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

String create_mqtt_json_keepalive(String topic, String ident, String fechayhora,
                                  String latitud, String longitud,
                                  String versionado,
                                  unsigned long rebootCount,
                                  String connType, String connDetail,
                                  String imei, String imsi, String iccid,
                                  String rsrq, String rsrp, String rssi) {
  StaticJsonDocument<512> doc;
  doc["topic"] = topic;
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

  char payload[512];
  serializeJson(doc, payload);
  return String(payload);
}

String create_mqtt_json_ble(String topic, String ident, String fechayhora,
                            const MokoSensorData& data,
                            String latitud, String longitud, float Vbateria,
                            float Vprincipal, unsigned long numeroPaquete) {
  StaticJsonDocument<2048> doc;
  
  doc["topic"] = topic;
  if (data.name.length() > 0) doc["name"] = data.name;
  if (data.tag_id.length() > 0) doc["tag_id"] = data.tag_id;
  if (data.uuid.length() > 0) doc["UUID"] = data.uuid;
  
  doc["date"] = fechayhora;
  doc["index"] = numeroPaquete;
  
  if (data.rawHex.length() > 0) doc["rawHex"] = data.rawHex;

  if (data.frameType != 0) {
    char ftBuf[5];
    sprintf(ftBuf, "0x%02X", data.frameType);
    doc["frame_type"] = String(ftBuf);
  }

  // Common or historically present fields (L02S, PaPeR, H4Pro general)
  if (data.batteryLevel > 0) doc["% bat"] = data.batteryLevel;
  if (data.rangingData != 0) doc["ranging"] = data.rangingData;
  if (data.advInterval > 0) doc["adv_int"] = data.advInterval;
  if (data.deviceType != 0) doc["dev_type"] = data.deviceType;
  if (data.motion > 0) doc["mov"] = data.motion;
  if (data.door > 0) doc["door"] = data.door;

  // Frame-specific conditional additions
  switch (data.frameType) {
    case 0x40:
      doc["dev_prop"] = data.deviceProperty;
      doc["sw_status"] = data.switchStatus;
      doc["firmware"] = data.firmwareVersion;
      break;
    case 0x50:
      doc["ibeacon_uuid"] = data.ibeaconUuid;
      doc["major"] = data.major;
      doc["minor"] = data.minor;
      doc["rssi1m"] = data.rssi1m;
      break;
    case 0x60:
      doc["samp_rate"] = data.samplingRate;
      doc["full_scale"] = data.fullScale;
      doc["motion_thr"] = data.motionThresh;
      // Acceleration is kept outside the switch so other sensors can use it too
      break;
    case 0x70:
      doc["temp"] = String(data.temperature, 2);
      doc["hum"] = String(data.humidity, 2);
      break;
    default:
      // If no specific MOKO frame type is set, it might be an older sensor
      // that relies on these globals. Add them if they're populated.
      if (data.temperature != 0.0) doc["temp"] = String(data.temperature, 2);
      if (data.humidity != 0.0) doc["hum"] = String(data.humidity, 2);
      break;
  }

  // Universal check for acceleration (used by 0x60 and other legacy tags)
  if (data.accelX != 0 || data.accelY != 0 || data.accelZ != 0) {
    doc["Accel_X"] = data.accelX;
    doc["Accel_Y"] = data.accelY;
    doc["Accel_Z"] = data.accelZ;
  }

  char payload[2048];
  serializeJson(doc, payload);
  return String(payload);
}
