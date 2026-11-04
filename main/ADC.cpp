#include "ADC.h"
#include "Debug.h"
#include "esp_adc_cal.h"
#include <Preferences.h>

#define ADC_PIN_34 34  // Pin ADC
#define ADC_PIN_39 39  // Pin ADC

// Usados para sensores seriales
// #define ADC1_CH4 32     // Pin ADC1_CH4
// #define ADC1_CH5 33     // Pin ADC1_CH5

esp_adc_cal_characteristics_t adc_chars;

extern float filterADC[2][2];
extern float ADCValueAnt[2];
extern int cantMed;
extern float paramADC[2][2];

// Conversor de ADC a Voltaje calibrado por Vref (eFuse)
float readVoltage(int pin) {
  uint32_t sum = 0;

  for (int i = 0; i < 64; i++) {
    sum += analogRead(pin);
  }

  uint32_t raw = sum / 64;
  uint32_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

  return mv / 1000.0f;
}

// Inicializacion del ADC para lectura de voltaje
void initADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN_34, ADC_11db);
  analogSetPinAttenuation(ADC_PIN_39, ADC_11db);

  esp_adc_cal_characterize(
      ADC_UNIT_1,
      ADC_ATTEN_DB_11,
      ADC_WIDTH_BIT_12,
      1100,               // Vref nominal
      &adc_chars
  );

  Preferences preferences;

  preferences.begin("adc_config", true);
  for (int i = 0; i < 2; i++) {
    String keyMin = "fil_" + String(i) + "_0";
    String keyVal = "fil_" + String(i) + "_1";
    filterADC[i][0] = preferences.getFloat(keyMin.c_str(), filterADC[i][0]);
    filterADC[i][1] = preferences.getFloat(keyVal.c_str(), filterADC[i][1]);

    // Cargar paramADC (Factor y Offset)
    String keyFactor = "param_" + String(i) + "_0";
    String keyOffset = "param_" + String(i) + "_1";
    paramADC[i][0] = preferences.getFloat(keyFactor.c_str(), paramADC[i][0]);
    paramADC[i][1] = preferences.getFloat(keyOffset.c_str(), paramADC[i][1]);
  }
  cantMed = preferences.getInt("cantMed", 50);
  preferences.end();
}

// Lectura de la entrada ADC pin 34
float leer_adc_pin_34() {
  float raw = readVoltage(ADC_PIN_34);
  return raw;
}

// Lectura de la entrada ADC pin 39
float leer_adc_pin_39() {
  float raw = readVoltage(ADC_PIN_39);
  return raw;
}

// Procesamiento de datos de ADC
void procesarADC(float ADCValue[]) {

  // Leo valores de los ADC
  ADCValue[0] = leer_adc_pin_34();
  ADCValue[1] = leer_adc_pin_39();

  // Aplico filtro por minimo tolerable
  if (ADCValue[0] <= filterADC[0][0]) {
    ADCValue[0] = filterADC[0][1];
  }
  if (ADCValue[1] <= filterADC[1][0]) {
    ADCValue[1] = filterADC[1][1];
  }

  // Aplico Alisado (MA)
  ADCValue[0] = ((ADCValueAnt[0] * (cantMed - 1)) + ADCValue[0]) / cantMed;
  ADCValueAnt[0] = ADCValue[0];

  ADCValue[1] = ((ADCValueAnt[1] * (cantMed - 1)) + ADCValue[1]) / cantMed;
  ADCValueAnt[1] = ADCValue[1];

  // Aplico Factor y Offset
  ADCValue[0] = (ADCValue[0] * paramADC[0][0]) + paramADC[0][1];
  ADCValue[1] = (ADCValue[1] * paramADC[1][0]) + paramADC[1][1];

  // Limpio ruido residual cercano a cero para evitar notacion cientifica en MQTT.
  if (ADCValue[0] > -0.001 && ADCValue[0] < 0.001) {
    ADCValue[0] = 0.0;
  }
  if (ADCValue[1] > -0.001 && ADCValue[1] < 0.001) {
    ADCValue[1] = 0.0;
  }
}
