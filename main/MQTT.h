#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>

// Variables globales definidas en MQTT.cpp
extern String MQTT_BROKER;
extern int MQTT_PORT;
extern PubSubClient mqtt;

// Funciones públicas
bool mqttConnect();
void mqttCallback(char *topic, byte *payload, unsigned int len);
bool publish_mqtt_json(String topic, String jsonPayload);

String create_mqtt_json_adc(String ident, String fechayhora, float adc0,
                            float adc1, float adc2);

String create_mqtt_json_pivot(String ident, String fechayhora, int di1,
                              int di2, int sirena, int pivot_enabled, unsigned long index);

String create_mqtt_json_keepalive(String ident, String fechayhora,
                                  String latitud, String longitud,
                                  String versionado, unsigned long rebootCount,
                                  String connType, String connDetail,
                                  String imei, String imsi, String iccid,
                                  String rsrq, String rsrp, String rssi);

#endif