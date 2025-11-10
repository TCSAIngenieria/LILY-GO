#ifndef DS18B20_H
#define DS18B20_H

#include <OneWire.h>
#include <DallasTemperature.h>
#include "SensorInterface.h"

#define ONE_WIRE_BUS 4
#define SENSOR_POWER_PIN 25

class DS18B20 : public SensorInterface {
public:
  DS18B20();
  void begin() override;
  float readValue() override;
  void loop() override;
  String getString();

private:
  void initSensor();
  OneWire oneWire;
  DallasTemperature sensors;
  DeviceAddress address;

  float ultimaTempValida = -999.0;
  unsigned long tiempoUltimoCambio = 0;
  const unsigned long SENSOR_TIEMPO_MAX_IGUAL = 300000;

  enum Estado { IDLE,
                APAGADO,
                ENCENDIDO };
  Estado estadoReinicio = IDLE;
  unsigned long tiempoReinicio = 0;

  // 🔧 NUEVO: bandera para saber si el sensor fue detectado
  bool sensorEncontrado = false;
};

#endif
