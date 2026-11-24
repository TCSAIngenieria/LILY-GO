#ifndef MOTOR_EVENTOS_H
#define MOTOR_EVENTOS_H

#include <Arduino.h>

#define MAX_EVENTOS 4
#define MAX_DT_TRIGGERS 4

#define PIN_EVT_DI1 13
#define PIN_EVT_DI2 14
#define PIN_EVT_BTN 0

enum EventSource {
  EVT_SRC_NONE = 0,
  EVT_SRC_VPRIN,
  EVT_SRC_VBACK,
  EVT_SRC_ADC0,
  EVT_SRC_ADC1,
  EVT_SRC_DI1,
  EVT_SRC_DI2,
  EVT_SRC_COMM,
  // 1. Sensores Físicos e Industriales Cableados
  EVT_SRC_TEMP,      // Temperatura DS18B20 (°C)
  EVT_SRC_S0,        // S0..S15 Canales de Expansora Serial
  EVT_SRC_S1,
  EVT_SRC_S2,
  EVT_SRC_S3,
  EVT_SRC_S4,
  EVT_SRC_S5,
  EVT_SRC_S6,
  EVT_SRC_S7,
  EVT_SRC_S8,
  EVT_SRC_S9,
  EVT_SRC_S10,
  EVT_SRC_S11,
  EVT_SRC_S12,
  EVT_SRC_S13,
  EVT_SRC_S14,
  EVT_SRC_S15,
  // 2. Sensores Inalámbricos BLE (MOKO)
  EVT_SRC_BLE_DOOR,  // Sensor Magnético de Apertura (1=abierto, 0=cerrado)
  EVT_SRC_BLE_MOV,   // Sensor PIR / Movimiento (1=movimiento, 0=reposo)
  EVT_SRC_BLE_TEMP,  // Temperatura BLE (°C)
  EVT_SRC_BLE_HUM,   // Humedad BLE (%)
  EVT_SRC_BLE_BAT,   // Batería sensor BLE (%)
  // 3. Salud y Diagnóstico del Hardware (ESP32)
  EVT_SRC_TEMP_MCU,  // Temperatura interna ESP32 (°C)
  EVT_SRC_HEAP,      // Memoria RAM libre (bytes)
  EVT_SRC_BTN,       // Botón físico IO0 (0=presionado, 1=suelto)
  // 4. Disparadores Programables por Fecha y Hora (4 índices: DT0 a DT3)
  EVT_SRC_DT0,       // Disparador Fecha/Hora 0 (1=coincide, 0=no coincide)
  EVT_SRC_DT1,       // Disparador Fecha/Hora 1 (1=coincide, 0=no coincide)
  EVT_SRC_DT2,       // Disparador Fecha/Hora 2 (1=coincide, 0=no coincide)
  EVT_SRC_DT3        // Disparador Fecha/Hora 3 (1=coincide, 0=no coincide)
};

enum EventOperator {
  EVT_OP_NONE = 0,
  EVT_OP_GT,   // >
  EVT_OP_LT,   // <
  EVT_OP_GTE,  // >=
  EVT_OP_LTE,  // <=
  EVT_OP_EQ,   // ==
  EVT_OP_NEQ   // !=
};

enum EventActionType {
  EVT_ACT_MQTT = 0,
  EVT_ACT_OUT_ON,
  EVT_ACT_OUT_OFF,
  EVT_ACT_OUT_PULSE,
  EVT_ACT_CYCLE_PWR,
  EVT_ACT_RESTART
};

enum EventTriggerMode {
  EVT_TRIG_ONESHOT = 0,
  EVT_TRIG_REPEAT
};

struct EventRule {
  bool enabled;
  EventSource source;
  char bleMac[13]; // MAC del sensor BLE específico (ej. "D47DA699A56B") o vacío para genérico
  EventOperator op;
  float threshold;
  uint32_t tonSeconds;
  EventActionType actionType;
  uint8_t outPin;
  uint32_t pulseSeconds;
  EventTriggerMode triggerMode;
  uint32_t repeatSeconds;

  // Variables dinámicas en RAM (no persistidas)
  unsigned long conditionStartTime;
  bool conditionMet;
  bool eventFired;
  unsigned long lastFiredTime;
  unsigned long pulseStartTime;
  bool pulseActive;
};

struct DateTimeTrigger {
  bool enabled;
  int16_t year;   // -1 = comodín (*), o 2020..2099
  int8_t  month;  // -1 = comodín (*), o 1..12
  int8_t  day;    // -1 = comodín (*), o 1..31
  int8_t  hour;   // -1 = comodín (*), o 0..23
  int8_t  minute; // -1 = comodín (*), o 0..59
  int8_t  second; // -1 = comodín (*), o 0..59
};

// Funciones de inicialización y ciclo
void initMotorEventos();
void procesarMotorEventos(float vPrin, float vBack, float adc0, float adc1);

// Actualización de datos de balizas BLE
void actualizarDatosBLE(const String &mac, float temp, float hum, float door, float mov, float bat);

// Configuración y consulta de disparadores por Fecha y Hora (DT0 a DT3)
bool configurarDisparadorFechaHora(int id, const String &paramStr, String &err);
String obtenerDisparadorFechaHora(int id);
String obtenerTodosDisparadoresFechaHora();

// Configuración y diagnóstico de reglas
bool configurarEvento(int id, const String &paramStr, String &err);
String obtenerConfiguracionEvento(int id);
String obtenerEstadoEventos(float vPrin, float vBack, float adc0, float adc1);

// Utilidades de conversión texto <-> enumeradores
String eventSourceToString(EventSource src, const char *bleMac = nullptr);
EventSource stringToEventSource(const String &str, char *outMac = nullptr);
String eventOpToString(EventOperator op);
EventOperator stringToEventOp(const String &str);
String eventActionToString(EventActionType act, uint8_t pin, uint32_t pulseSec);
String eventTriggerToString(EventTriggerMode trig, uint32_t repeatSec);

#endif
