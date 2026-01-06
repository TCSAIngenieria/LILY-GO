#ifndef BLE_MOKO_H
#define BLE_MOKO_H

#include <Arduino.h>
// Reordering includes: NimBLEDevice.h should be first as it sets up the
// environment
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEUtils.h>

struct MokoSensorData {
  bool valid;
  String name;
  String mac;
  int rssi;
  float temperature;
  float humidity;
  int batteryLevel;
  float accelX;
  float accelY;
  float accelZ;
  String rawHex;
  uint8_t motion;
  uint8_t door;
  String tag_id;
  String uuid;
  unsigned long lastUpdate;
};

class BLEMokoScanner {
public:
  BLEMokoScanner();
  void begin();
  void loop();
  MokoSensorData getLatestData();
  bool hasNewData();

private:
  NimBLEScan *pBLEScan;
  MokoSensorData latestData;
  bool newDataAvailable;
  unsigned long lastScanTime;
  const unsigned long SCAN_INTERVAL = 30000; // Scan every 30 seconds
  const int SCAN_DURATION = 5;               // Scan for 5 seconds

  friend class MyAdvertisedDeviceCallbacks;
  bool _initialized;
};

#endif
