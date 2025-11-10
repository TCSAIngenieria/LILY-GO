#ifndef SENSOR_INTERFACE_H
#define SENSOR_INTERFACE_H

class SensorInterface {
public:
  virtual void begin() = 0;
  virtual float readValue() = 0;
  virtual void loop() = 0;
};

#endif
