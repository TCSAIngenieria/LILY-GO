#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include "BLE_MOKO.h"

// Variables globales definidas en MQTT.cpp
extern String MQTT_BROKER;
extern int MQTT_PORT;
extern PubSubClient mqtt;

// Funciones públicas
bool mqttConnect();
void mqttCallback(char *topic, byte *payload, unsigned int len);
bool publish_mqtt_json(String topic, String jsonPayload);

String create_mqtt_json_sensor(String topic, String ident,
                               String valor_variable, String fechayhora,
                               float Vbackup,
                               float Vprincipal, unsigned long numeroPaquete);

String create_mqtt_json_serial(String topic, String ident, String S0, String S1,
                               String S2, String S3, String S4, String S5,
                               String S6, String S7, String S8, String S9,
                               String S10, String S11, String S12, String S13,
                               String S14, String S15, String fechayhora,
                               String latitud, String longitud, float Vbackup,
                               float Vprincipal, unsigned long numeroPaquete);

String create_mqtt_json_modbus(String topic, String ident, String fechayhora,
                               String latitud, String longitud, float Vbackup,
                               float Vprincipal, unsigned long numeroPaquete);

String create_mqtt_json_ble(String topic, String ident, String fechayhora,
                            const MokoSensorData& data,
                            String latitud, String longitud, float Vbackup,
                            float Vprincipal, unsigned long numeroPaquete);

String create_mqtt_json_adc(String topic, String ident, String fechayhora, float adc0,
                            float adc1);

String create_mqtt_json_keepalive(String topic, String ident, String fechayhora,
                                  String latitud, String longitud,
                                  String versionado, unsigned long rebootCount,
                                  String connType, String connDetail,
                                  String imei, String imsi, String iccid,
                                  String rsrq, String rsrp, String rssi,
                                  float Vbackup, float Vprincipal);

#endif
