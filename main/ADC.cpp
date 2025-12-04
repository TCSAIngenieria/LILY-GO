#include "ADC.h"
#include <Preferences.h>

#define ADC_PIN_BAT 34 // Ejemplo pin ADC para bateria (18650)
#define ADC1_CH7 35    // Pin ADC1_CH7
#define ADC1_CH0 36    // Pin ADC1_CH0
#define ADC1_CH3 39    // Pin ADC1_CH3

// Usados para sensores seriales
// #define ADC1_CH4 32     // Pin ADC1_CH4
// #define ADC1_CH5 33     // Pin ADC1_CH5

extern float filterADC[3][2];
extern float ADCValueAnt[3];
extern int cantMed;
extern float paramADC[3][2];

// Conversor de ADC a Voltaje
float readVoltage(int pin) {
  int adcValue = analogRead(pin);
  float voltage = (adcValue / 4095.0) * 3.3;
  return voltage;
}

// Inicializacion del ADC para lectura de voltaje
void initADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN_BAT, ADC_11db);

  Preferences preferences;

  preferences.begin("adc_config", true);
  for (int i = 0; i < 3; i++) {
    String keyMin = "fil_" + String(i) + "_0";
    String keyVal = "fil_" + String(i) + "_1";
    filterADC[i][0] = preferences.getFloat(keyMin.c_str(), filterADC[i][0]);
    filterADC[i][1] = preferences.getFloat(keyVal.c_str(), filterADC[i][1]);
  }
  cantMed = preferences.getInt("cantMed", 50);
  preferences.end();
}

// Lectura de voltaje de bateria
float leer_tension_bateria() {
  float raw = readVoltage(ADC_PIN_BAT);

  // Ajuste por divisor resistivo: si 4.2V reales ⇒ 1.6V leidos
  float batteryVoltage = raw * (4.2 / 2.56);

  Serial.print("Voltaje bateria estimado (18650): ");
  Serial.print(batteryVoltage);
  Serial.println(" V");

  return batteryVoltage;
}

// Infiere la tension de alimentacion principal
float leer_tension_principal() {
  float batteryVoltage =
      leer_tension_bateria(); // Ya retorna el valor corregido

  // Si la bateria esta bien cargada (>4V reales aprox.)
  if (batteryVoltage > 4) {
    Serial.println("Alimentacion principal estimada: 5.0 V");
    return 5.0;
  } else {
    Serial.println("Alimentacion principal estimada: 0.0 V");
    return 0.0;
  }
}

// Lectura de voltaje de ADC1_CH7
float leer_tension_adc1_ch7() {
  float raw = readVoltage(ADC1_CH7);
  return raw;
}

// Lectura de voltaje de ADC1_CH0
float leer_tension_adc1_ch0() {
  float raw = readVoltage(ADC1_CH0);
  return raw;
}

// Lectura de voltaje de ADC1_CH3
float leer_tension_adc1_ch3() {
  float raw = readVoltage(ADC1_CH3);
  return raw;
}

// Procesamiento de datos de ADC
void procesarADC(float ADCValue[]) {

  // Leo valores de los ADC
  ADCValue[0] = leer_tension_adc1_ch7();
  ADCValue[1] = leer_tension_adc1_ch0();
  ADCValue[2] = leer_tension_adc1_ch3();

  // Aplico filtro por minimo tolerable
  if (ADCValue[0] <= filterADC[0][0]) {
    ADCValue[0] = filterADC[0][1];
  }
  if (ADCValue[1] <= filterADC[1][0]) {
    ADCValue[1] = filterADC[1][1];
  }
  if (ADCValue[2] <= filterADC[2][0]) {
    ADCValue[2] = filterADC[2][1];
  }

  // Aplico Alisado (MA)
  ADCValue[0] = ((ADCValueAnt[0] * (cantMed - 1)) + ADCValue[0]) / cantMed;
  ADCValueAnt[0] = ADCValue[0];

  ADCValue[1] = ((ADCValueAnt[1] * (cantMed - 1)) + ADCValue[1]) / cantMed;
  ADCValueAnt[1] = ADCValue[1];

  ADCValue[2] = ((ADCValueAnt[2] * (cantMed - 1)) + ADCValue[2]) / cantMed;
  ADCValueAnt[2] = ADCValue[2];

  // Aplico Factor y Offset
  ADCValue[0] = (ADCValue[0] * paramADC[0][0]) + paramADC[0][1];
  ADCValue[1] = (ADCValue[1] * paramADC[1][0]) + paramADC[1][1];
  ADCValue[2] = (ADCValue[2] * paramADC[2][0]) + paramADC[2][1];
}