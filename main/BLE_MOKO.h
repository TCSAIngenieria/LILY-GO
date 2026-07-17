#ifndef BLE_MOKO_H
#define BLE_MOKO_H

#include <Arduino.h>
// Reordering includes: NimBLEDevice.h should be first as it sets up the
// environment
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEUtils.h>
#include <vector>

struct MokoSensorData {
  bool valid = false;
  String name = "";
  String mac = "";
  int rssi = 0;
  float temperature = 0.0;
  float humidity = 0.0;
  int batteryLevel = 0;
  uint16_t batteryMv = 0;
  bool batteryValid = false;
  int rangingData = 0;
  int advInterval = 0;
  int deviceType = 0;
  uint16_t manufacturerId = 0;
  uint8_t frameType = 0;
  uint8_t deviceProperty = 0;
  uint8_t switchStatus = 0;
  String firmwareVersion = "";
  String ibeaconUuid = "";
  uint16_t major = 0;
  uint16_t minor = 0;
  int8_t rssi1m = 0;
  uint8_t samplingRate = 0;
  uint8_t fullScale = 0;
  uint8_t motionThresh = 0;
  float accelX = 0;
  float accelY = 0;
  float accelZ = 0;
  String rawHex = "";
  uint8_t motion = 0;
  uint8_t door = 0;
  bool doorOpen = false;
  bool hasDoorOpen = false;
  uint8_t pirRaw = 0;
  bool hasPirRaw = false;
  bool pirValid = false;
  bool hasMotionStatus = false;
  bool hasDoorStatus = false;
  String tag_id = "";
  String uuid = "";
  unsigned long lastUpdate = 0;
};

class BLEMokoScanner {
public:
  BLEMokoScanner();
  void begin();
  void loop();
  std::vector<MokoSensorData> getLatestData();
  bool hasNewData();

private:
  NimBLEScan *pBLEScan;
  std::vector<MokoSensorData> latestData;
  bool newDataAvailable;
  unsigned long lastScanTime;
  const unsigned long SCAN_INTERVAL = 30000; // Scan every 30 seconds
  const int SCAN_DURATION = 5;               // Scan for 5 seconds

  friend class MyAdvertisedDeviceCallbacks;
  bool _initialized;
};

#endif
