#ifndef MQTT_H
#define MQTT_H

#include <PubSubClient.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

// Configuración del broker

#define MQTT_USER     "DVL_test1"
#define MQTT_PASSWORD "DVL_test1"
#define MQTT_ID  "DVL_V1"


extern String MQTT_BROKER;
extern int MQTT_PORT;

boolean mqttConnect();
void mqttCallback(char *topic, byte *payload, unsigned int len);
bool publish_mqtt_json(String topic, String jsonPayload);
String create_mqtt_json_sensor(String topic, String ident, String valor_variable, String fechayhora, String latitud, String longitud, float Vbateria, float Vprincipal, unsigned long numeroPaquete);
String create_mqtt_json_serial(String topic, String ident, String S0, String S1, String S2, String S3, String S4, String S5,
String S6, String S7, String S8, String S9, String S10, String S11, String S12, String S13, String S14, String S15, String fechayhora, String latitud, String longitud, float Vbateria, float Vprincipal, unsigned long numeroPaquete);

#endif
