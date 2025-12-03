
#ifndef ADC_H
#define ADC_H

#include <Arduino.h>

float leer_tension_bateria();
float leer_tension_principal();
float leer_tension_adc1_ch7();
float leer_tension_adc1_ch0();
float leer_tension_adc1_ch3();
void procesarADC(float ADCValue[]);
void initADC();

#endif
