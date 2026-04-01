#include "ADC.h"

#define ADC_PIN_BAT 34       // Ejemplo pin ADC para bateria (18650)
#define ADC_PIN_ADC1_CH7 35  // Pin ADC1_CH7

float readVoltage(int pin) {
  int adcValue = analogRead(pin);
  float voltage = (adcValue / 4095.0) * 3.3;  // Conversion ADC a voltaje
  return voltage;
}

void initADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN_BAT, ADC_11db);  // Hasta 3.3V
}

float leer_tension_bateria() {
  float raw = readVoltage(ADC_PIN_BAT);

  // Ajuste por divisor resistivo: si 4.2V reales ⇒ 1.6V leidos
  float batteryVoltage = raw * (4.2 / 2.56);

  Serial.print("Voltaje bateria estimado (18650): ");
  Serial.print(batteryVoltage);
  Serial.println(" V");

  return batteryVoltage;
}

float leer_tension_principal() {
  float batteryVoltage = leer_tension_bateria();  // Ya retorna el valor corregido

  // Si la bateria esta bien cargada (>4V reales aprox.)
  if (batteryVoltage > 4) {
    Serial.println("Alimentacion principal estimada: 5.0 V");
    return 5.0;
  } else {
    Serial.println("Alimentacion principal estimada: 0.0 V");
    return 0.0;
  }
}

float leer_tension_adc1_ch7() {
  float raw = readVoltage(ADC_PIN_ADC1_CH7);

  Serial.print("Voltaje ADC2_0 leido: ");
  Serial.print(raw);
  Serial.println(" V");

  return raw;
}
