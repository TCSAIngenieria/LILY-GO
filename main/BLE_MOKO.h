#ifndef BLE_MOKO_H
#define BLE_MOKO_H

#include <Arduino.h>
#include <vector>

struct MokoSensorData {
  bool valid = false;
  float temperature = 0.0;
  String mac = "";
  uint8_t frameType = 0;
};

class BLEMokoScanner {
public:
  BLEMokoScanner() {}
  void begin() {}
  void loop() {}
  std::vector<MokoSensorData> getLatestData() { return std::vector<MokoSensorData>(); }
  bool hasNewData() { return false; }
};

#endif
