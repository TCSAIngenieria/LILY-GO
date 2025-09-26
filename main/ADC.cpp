#include "ADC.h"

#define ADC_PIN_BAT  34  // Ejemplo pin ADC para batería (18650)
#define ADC_PIN_5V   32  // Ya no se usará directamente

float readVoltage(int pin) {
  int adcValue = analogRead(pin);
  float voltage = (adcValue / 4095.0) * 3.3;  // Conversión ADC a voltaje
  return voltage;
}

void initADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN_BAT, ADC_11db); // Hasta 3.3V
  // ADC_PIN_5V no se usa más porque vamos a estimar en base al voltaje de batería
}

float leer_tension_bateria() {
  float raw = readVoltage(ADC_PIN_BAT);

  // Ajuste por divisor resistivo: si 4.2V reales ⇒ 1.6V leídos
  float batteryVoltage = raw * (4.2 / 2.56);

  Serial.print("Voltaje batería estimado (18650): ");
  Serial.print(batteryVoltage);
  Serial.println(" V");

  return batteryVoltage;
}

float leer_tension_principal() {
  float batteryVoltage = leer_tension_bateria();  // Ya retorna el valor corregido

  // Si la batería está bien cargada (>4V reales aprox.)
  if (batteryVoltage > 4) {
    Serial.println("Alimentación principal estimada: 5.0 V");
    return 5.0;
  } else {
    Serial.println("Alimentación principal estimada: 0.0 V");
    return 0.0;
  }
}
