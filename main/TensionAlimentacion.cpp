#include "TensionAlimentacion.h"
#include "Debug.h"

#define PIN_ALIMENTACION_BACKUP 35
#define PIN_ALIMENTACION_PRINCIPAL 36
#define FACTOR_DIVISOR_RESISTIVO 2.0
#define FACTOR_ALIMENTACION_BACKUP 1.0916039
#define OFFSET_ALIMENTACION_BACKUP 0.0
#define FACTOR_ALIMENTACION_PRINCIPAL 1.0813719
#define OFFSET_ALIMENTACION_PRINCIPAL 0.0

static float readVoltageTensionAlimentacion(int pin) {
  int adcValue = analogRead(pin);
  float voltage = (adcValue / 4095.0) * 3.3;
  return voltage;
}

static float aplicarCalibracionTensionAlimentacion(float raw, float factor,
                                                   float offset) {
  return (raw * FACTOR_DIVISOR_RESISTIVO * factor) + offset;
}

void initTensionAlimentacion() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ALIMENTACION_BACKUP, ADC_11db);
  analogSetPinAttenuation(PIN_ALIMENTACION_PRINCIPAL, ADC_11db);
}

// Lectura de voltaje de alimentacion backup
float leer_tension_bateria() {
  float raw = readVoltageTensionAlimentacion(PIN_ALIMENTACION_BACKUP);
  float voltage = aplicarCalibracionTensionAlimentacion(
      raw, FACTOR_ALIMENTACION_BACKUP, OFFSET_ALIMENTACION_BACKUP);

  DVL_PRINT("Voltaje alimentacion backup: ");
  DVL_PRINT(voltage);
  DVL_PRINTLN(" V");

  return voltage;
}

// Lectura de voltaje de alimentacion principal
float leer_tension_principal() {
  float raw = readVoltageTensionAlimentacion(PIN_ALIMENTACION_PRINCIPAL);
  float voltage = aplicarCalibracionTensionAlimentacion(
      raw, FACTOR_ALIMENTACION_PRINCIPAL, OFFSET_ALIMENTACION_PRINCIPAL);

  DVL_PRINT("Voltaje alimentacion principal: ");
  DVL_PRINT(voltage);
  DVL_PRINTLN(" V");

  return voltage;
}
