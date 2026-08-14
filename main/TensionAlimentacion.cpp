#include "TensionAlimentacion.h"
#include "Debug.h"

#define PIN_TENSION_BACKUP 35    // IO35 / VVBAT
#define PIN_TENSION_PRINCIPAL 36 // GPIO36 / S_VP / SOLAR_IN
#define FACTOR_DIVISOR_RESISTIVO 2.0f
#define FACTOR_TENSION_BACKUP 0.9952f
#define OFFSET_TENSION_BACKUP 0.0f
#define FACTOR_TENSION_PRINCIPAL 0.991006f
#define OFFSET_TENSION_PRINCIPAL 0.0f
#define MUESTRAS_TENSION_ALIMENTACION 8

static float readVoltageTensionAlimentacion(int pin) {
  uint32_t sumaMv = 0;
  for (int i = 0; i < MUESTRAS_TENSION_ALIMENTACION; i++) {
    sumaMv += analogReadMilliVolts(pin);
  }
  return (sumaMv / (float)MUESTRAS_TENSION_ALIMENTACION) / 1000.0f;
}

static float aplicarCalibracionTensionAlimentacion(float raw, float factor,
                                                   float offset) {
  return (raw * FACTOR_DIVISOR_RESISTIVO * factor) + offset;
}

void initTensionAlimentacion() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TENSION_BACKUP, ADC_11db);
  analogSetPinAttenuation(PIN_TENSION_PRINCIPAL, ADC_11db);
}

// Lectura de voltaje de alimentacion backup
float leer_tension_backup() {
  float raw = readVoltageTensionAlimentacion(PIN_TENSION_BACKUP);
  float voltage = aplicarCalibracionTensionAlimentacion(
      raw, FACTOR_TENSION_BACKUP, OFFSET_TENSION_BACKUP);

  DVL_PRINT("Tension_backup (IO35/VVBAT): ");
  DVL_PRINT(voltage);
  DVL_PRINTLN(" V");

  return voltage;
}

// Lectura de voltaje de alimentacion principal
float leer_tension_principal() {
  float raw = readVoltageTensionAlimentacion(PIN_TENSION_PRINCIPAL);
  float voltage = aplicarCalibracionTensionAlimentacion(
      raw, FACTOR_TENSION_PRINCIPAL, OFFSET_TENSION_PRINCIPAL);

  DVL_PRINT("Tension_principal (GPIO36/SOLAR_IN): ");
  DVL_PRINT(voltage);
  DVL_PRINTLN(" V");

  return voltage;
}
