
#ifndef ADC_H
#define ADC_H

#include <Arduino.h>

float leer_tension_adc_pin_34();
float leer_tension_adc_pin_39();
void procesarADC(float ADCValue[]);
void initADC();

#endif
