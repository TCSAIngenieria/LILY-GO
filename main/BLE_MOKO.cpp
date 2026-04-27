#include "BLE_MOKO.h"
#include "Debug.h"

#include <vector>

static std::vector<MokoSensorData> _tempDataList;
static bool _foundDevice = false;

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    DVL_PRINT("BLE Found: ");
    DVL_PRINT(advertisedDevice->getName().c_str());
    DVL_PRINT(" MAC: ");
    DVL_PRINT(advertisedDevice->getAddress().toString().c_str());
    DVL_PRINT(" RSSI: ");
    DVL_PRINTLN(advertisedDevice->getRSSI());

    std::string macAddress = advertisedDevice->getAddress().toString();

    // ==================== MOKO BEACONX PRO (H4 Pro) ====================
    bool isMoko = false;
    const std::vector<uint8_t> &payload_check = advertisedDevice->getPayload();
    for (size_t i = 0; i + 1 < payload_check.size(); i++) {
      if (payload_check[i] == 0x16 && i + 3 < payload_check.size() &&
          payload_check[i + 1] == 0xAB && payload_check[i + 2] == 0xFE) {
        isMoko = true;
        break;
      }
    }

    if (isMoko) {
      DVL_PRINTLN("================================================");
      DVL_PRINTLN(">>> MOKO BEACONX PRO FOUND <<<");
      DVL_PRINTLN("================================================");

      // --- Basic Info ---
      DVL_PRINT("  Name: ");
      if (advertisedDevice->haveName()) {
        DVL_PRINTLN(advertisedDevice->getName().c_str());
      } else {
        DVL_PRINTLN("(none)");
      }
      DVL_PRINT("  MAC: ");
      DVL_PRINTLN(macAddress.c_str());
      DVL_PRINT("  RSSI: ");
      DVL_PRINTLN(advertisedDevice->getRSSI());
      DVL_PRINT("  Address Type: ");
      DVL_PRINTLN(advertisedDevice->getAddress().getType());

      // --- Manufacturer Data ---
      DVL_PRINTLN("  --- Manufacturer Data ---");
      if (advertisedDevice->haveManufacturerData()) {
        std::string mfgData = advertisedDevice->getManufacturerData();
        DVL_PRINT("  Length: ");
        DVL_PRINTLN(mfgData.length());
        DVL_PRINT("  Hex: ");
        String mfgHex = "";
        for (size_t i = 0; i < mfgData.length(); i++) {
          char buf[4];
          sprintf(buf, "%02X ", (unsigned char)mfgData[i]);
          mfgHex += String(buf);
        }
        DVL_PRINTLN(mfgHex);
      } else {
        DVL_PRINTLN("  (none)");
      }

      // --- Service UUIDs ---
      DVL_PRINTLN("  --- Service UUIDs ---");
      if (advertisedDevice->haveServiceUUID()) {
        DVL_PRINT("  UUID: ");
        DVL_PRINTLN(advertisedDevice->getServiceUUID().toString().c_str());
      } else {
        DVL_PRINTLN("  (none)");
      }

      // --- Full Payload Hex Dump ---
      const std::vector<uint8_t> &payload = advertisedDevice->getPayload();
      DVL_PRINTLN("  --- Full Payload Hex Dump ---");
      DVL_PRINT("  Payload Length: ");
      DVL_PRINTLN(payload.size());
      String fullHex = "";
      for (size_t i = 0; i < payload.size(); i++) {
        char buf[8];
        sprintf(buf, "[%02d]%02X ", (int)i, payload[i]);
        fullHex += String(buf);
        if ((i + 1) % 8 == 0) {
          DVL_PRINT("  ");
          DVL_PRINTLN(fullHex);
          fullHex = "";
        }
      }
      if (fullHex.length() > 0) {
        DVL_PRINT("  ");
        DVL_PRINTLN(fullHex);
      }

      // --- Build rawHex string ---
      String rawHexStr = "";
      for (size_t i = 0; i < payload.size(); i++) {
        char buf[3];
        sprintf(buf, "%02X", payload[i]);
        rawHexStr += String(buf);
      }

      // --- Prepare MokoSensorData ---
      MokoSensorData h4Data;
      h4Data.valid = false;
      if (advertisedDevice->haveName()) {
        h4Data.name = String(advertisedDevice->getName().c_str());
      } else {
        h4Data.name = "unknown";
      }
      String cleanMacH4 = String(macAddress.c_str());
      cleanMacH4.replace(":", "");
      h4Data.mac = cleanMacH4;
      h4Data.rssi = advertisedDevice->getRSSI();
      h4Data.rawHex = rawHexStr;
      h4Data.lastUpdate = millis();
      h4Data.accelX = 0;
      h4Data.accelY = 0;
      h4Data.accelZ = 0;
      h4Data.motion = 0;
      h4Data.door = 0;
      h4Data.tag_id = "";
      h4Data.uuid = "";
      h4Data.temperature = 0;
      h4Data.humidity = 0;
      h4Data.batteryLevel = 0;
      h4Data.rangingData = 0;
      h4Data.advInterval = 0;
      h4Data.deviceType = 0;
      h4Data.frameType = 0;
      h4Data.deviceProperty = 0;
      h4Data.switchStatus = 0;
      h4Data.firmwareVersion = "";
      h4Data.ibeaconUuid = "";
      h4Data.major = 0;
      h4Data.minor = 0;
      h4Data.rssi1m = 0;
      h4Data.samplingRate = 0;
      h4Data.fullScale = 0;
      h4Data.motionThresh = 0;

      // --- Search for MOKO Service Data (UUID 0xFEAB) in payload ---
      for (size_t i = 0; i + 1 < payload.size(); i++) {
        // AD Type 0x16 = Service Data, followed by UUID 0xFEAB (little-endian: AB FE)
        if (payload[i] == 0x16 && i + 3 < payload.size() &&
            payload[i + 1] == 0xAB && payload[i + 2] == 0xFE) {
          uint8_t frameType = payload[i + 3];
          DVL_PRINT("  >>> MOKO Service Data found! Frame Type: 0x");
          char ftBuf[3];
          sprintf(ftBuf, "%02X", frameType);
          DVL_PRINTLN(ftBuf);
          h4Data.uuid = String(ftBuf);

          size_t frameStart = i + 3; // position of frameType byte

          h4Data.frameType = frameType;

          switch (frameType) {
            case 0x40: { // Device Info
              if (frameStart + 14 < payload.size()) {
                h4Data.rangingData = (int8_t)payload[frameStart + 1];
                h4Data.advInterval = payload[frameStart + 2] * 100;
                
                uint16_t battMv = ((uint16_t)payload[frameStart + 3] << 8) | payload[frameStart + 4];
                int battPct = constrain(map(battMv, 2000, 3600, 0, 100), 0, 100);
                h4Data.batteryLevel = battPct;
                
                h4Data.deviceProperty = payload[frameStart + 5];
                h4Data.switchStatus = payload[frameStart + 6];
                
                h4Data.tag_id = "";
                for (int j = 7; j <= 12; j++) {
                  char tagBuf[3];
                  sprintf(tagBuf, "%02X", payload[frameStart + j]);
                  h4Data.tag_id += String(tagBuf);
                }
                
                char fwBuf[16];
                sprintf(fwBuf, "V%d.%d.%d", payload[frameStart + 13] >> 4, payload[frameStart + 13] & 0x0F, payload[frameStart + 14] & 0x0F);
                h4Data.firmwareVersion = String(fwBuf);

                if (frameStart + 15 < payload.size()) {
                  h4Data.deviceType = payload[frameStart + 15];
                }

                DVL_PRINTF("  H4Pro [0x40] -> DevType: %d, Batt: %d%%, FW: %s, TagID: %s\n", h4Data.deviceType, h4Data.batteryLevel, fwBuf, h4Data.tag_id.c_str());
                h4Data.valid = true;
              }
              break;
            }
            case 0x50: { // iBeacon
              // Notice: iBeacon payload might use slightly different index for frameType, 
              // but assuming UUID 0xFEAB is still present and frameType is right after.
              if (frameStart + 18 < payload.size()) {
                h4Data.rssi1m = (int8_t)payload[frameStart + 1]; // -100~0dBm (similar to ranging)
                h4Data.advInterval = payload[frameStart + 2] * 100;
                
                h4Data.ibeaconUuid = "";
                for (int j = 3; j <= 18; j++) {
                  char uBuf[3];
                  sprintf(uBuf, "%02X", payload[frameStart + j]);
                  h4Data.ibeaconUuid += String(uBuf);
                }
                
                h4Data.major = ((uint16_t)payload[frameStart + 19] << 8) | payload[frameStart + 20];
                h4Data.minor = ((uint16_t)payload[frameStart + 21] << 8) | payload[frameStart + 22];

                DVL_PRINTF("  H4Pro [0x50] -> UUID: %s, Maj: %d, Min: %d\n", h4Data.ibeaconUuid.c_str(), h4Data.major, h4Data.minor);
                h4Data.valid = true;
              }
              break;
            }
            case 0x60: { // 3-axis ACC
              if (frameStart + 20 < payload.size()) {
                h4Data.rangingData = (int8_t)payload[frameStart + 1];
                h4Data.advInterval = payload[frameStart + 2] * 100;
                h4Data.samplingRate = payload[frameStart + 3];
                h4Data.fullScale = payload[frameStart + 4];
                h4Data.motionThresh = payload[frameStart + 5];

                int16_t rawX = ((uint16_t)payload[frameStart + 6] << 8) | payload[frameStart + 7];
                int16_t rawY = ((uint16_t)payload[frameStart + 8] << 8) | payload[frameStart + 9];
                int16_t rawZ = ((uint16_t)payload[frameStart + 10] << 8) | payload[frameStart + 11];

                // Simplified accel storage, true mg requires calculating based on scale
                h4Data.accelX = rawX;
                h4Data.accelY = rawY;
                h4Data.accelZ = rawZ;

                uint16_t battMv = ((uint16_t)payload[frameStart + 12] << 8) | payload[frameStart + 13];
                int battPct = constrain(map(battMv, 2000, 3600, 0, 100), 0, 100);
                h4Data.batteryLevel = battPct;

                h4Data.tag_id = "";
                for (int j = 15; j <= 20; j++) { // RFU byte skipped
                  char tagBuf[3];
                  sprintf(tagBuf, "%02X", payload[frameStart + j]);
                  h4Data.tag_id += String(tagBuf);
                }

                DVL_PRINTF("  H4Pro [0x60] -> ACC(raw): %d,%d,%d, Batt: %d%%\n", rawX, rawY, rawZ, battPct);
                h4Data.valid = true;
              }
              break;
            }
            case 0x70: { // T&H data
              if (frameStart + 15 < payload.size()) {
                // Tag ID (MAC from frame, index 20-25 -> frameStart + 10 to + 15)
                h4Data.tag_id = "";
                for (int j = 10; j <= 15; j++) {
                  char tagBuf[3];
                  sprintf(tagBuf, "%02X", payload[frameStart + j]);
                  h4Data.tag_id += String(tagBuf);
                }

                // Temperature (0.1 °C, index 13-14 -> frameStart + 3, 4)
                int16_t tempRaw = ((uint16_t)payload[frameStart + 3] << 8) | payload[frameStart + 4];
                h4Data.temperature = tempRaw / 10.0;

                // Humidity (0.1 %, index 15-16 -> frameStart + 5, 6)
                uint16_t humRaw = ((uint16_t)payload[frameStart + 5] << 8) | payload[frameStart + 6];
                h4Data.humidity = humRaw / 10.0;

                // Battery (mV, index 17-18 -> frameStart + 7, 8)
                uint16_t battMv = ((uint16_t)payload[frameStart + 7] << 8) | payload[frameStart + 8];
                int battPct = constrain(map(battMv, 2000, 3600, 0, 100), 0, 100);
                h4Data.batteryLevel = battPct;

                // Ranging data (Tx Power at 0m, signed int8, index 11 -> frameStart + 1)
                h4Data.rangingData = (int8_t)payload[frameStart + 1];

                // Adv interval (unsigned int8, unit: 100ms, index 12 -> frameStart + 2)
                h4Data.advInterval = payload[frameStart + 2] * 100;

                // Device type (unsigned int8, index 19 -> frameStart + 9)
                h4Data.deviceType = payload[frameStart + 9];

                DVL_PRINTF("  H4Pro [0x70] -> DevType: %d, Temp: %.1f C, Hum: %.1f %%, Batt: %d%%\n",
                              h4Data.deviceType, h4Data.temperature, h4Data.humidity, battPct);
                DVL_PRINTF("  Ranging: %d dBm, AdvInt: %d ms\n",
                              h4Data.rangingData, h4Data.advInterval);
                h4Data.valid = true;
              }
              break;
            }
            default:
              DVL_PRINTF("  H4Pro Unknown Frame Type: 0x%02X\n", frameType);
              break;
          }

          if (!h4Data.valid) {
            // Print raw bytes for any other frame type for discovery
            DVL_PRINT("  Frame 0x");
            DVL_PRINT(ftBuf);
            DVL_PRINT(" raw bytes: ");
            if (i > 0) {
              uint8_t adLen = payload[i - 1];
              String adHex = "";
              for (size_t j = i; j < i + adLen && j < payload.size(); j++) {
                char abuf[4];
                sprintf(abuf, "%02X ", payload[j]);
                adHex += String(abuf);
              }
              DVL_PRINTLN(adHex);
            }
          }
        }
      }

      // Even without T&H frame, save raw data
      if (!h4Data.valid) {
        DVL_PRINTLN("  (!) No T&H frame (0x70) found, saving raw data only");
        h4Data.valid = true;
      }

      _tempDataList.push_back(h4Data);
      _foundDevice = true;
      DVL_PRINTLN("================================================");
    }
    // ==================== END H4 PRO ====================

    // Check for "PaPeR" in the advertised name
    bool isTarget = false;
    if (advertisedDevice->haveName() && advertisedDevice->getName().find("PaPeR") != std::string::npos) {
        isTarget = true;
    }

    if (isTarget) {
      DVL_PRINTLN(">>> TARGET DEVICE FOUND (PaPeR) <<<");
      DVL_PRINT("MAC: ");
      DVL_PRINTLN(macAddress.c_str());
      DVL_PRINT("RSSI: ");
      DVL_PRINTLN(advertisedDevice->getRSSI());

      if (advertisedDevice->haveName()) {
        DVL_PRINT("Name: ");
        DVL_PRINTLN(advertisedDevice->getName().c_str());
      }

      if (advertisedDevice->haveManufacturerData()) {
        std::string data = advertisedDevice->getManufacturerData();
        DVL_PRINT("Manufacturer Data (Hex): ");
        String hexData = "";
        for (int i = 0; i < data.length(); i++) {
          char output[3];
          sprintf(output, "%02X", (unsigned char)data[i]);
          hexData += String(output);
        }
        DVL_PRINTLN(hexData);

        MokoSensorData _tempData;
        _tempData.rawHex = hexData;
        _tempData.valid = true;
        _tempData.name = "TARGET_DEVICE";
        _tempData.mac = macAddress.c_str();
        _tempData.rssi = advertisedDevice->getRSSI();
        _tempData.lastUpdate = millis();
      } else {
        DVL_PRINTLN("No Manufacturer Data");
      }

      if (advertisedDevice->haveServiceUUID()) {
        DVL_PRINT("Service UUID: ");
        DVL_PRINTLN(advertisedDevice->getServiceUUID().toString().c_str());
      }

      const std::vector<uint8_t> &payloadVector =
          advertisedDevice->getPayload();

      // PARSEO ESPECIFICO PARA MOKO L02S / PaPeR (Service Data 0xEA01)
      if (payloadVector.size() >= 28) {
        MokoSensorData _tempData;

        int16_t tempRaw =
            ((uint16_t)payloadVector[19] << 8) | payloadVector[20];
        _tempData.temperature = tempRaw / 10.0;

        uint16_t humRaw =
            ((uint16_t)payloadVector[21] << 8) | payloadVector[22];
        _tempData.humidity = humRaw / 10.0;

        uint16_t battMv =
            ((uint16_t)payloadVector[23] << 8) | payloadVector[24];
        int battPct = map(battMv, 2000, 3100, 0, 100);
        battPct = constrain(battPct, 0, 100);
        _tempData.batteryLevel = battPct;

        uint8_t statusByte = payloadVector[8];
        _tempData.motion = (statusByte >> 1) & 0x01;
        _tempData.door = (statusByte >> 0) & 0x01;

        _tempData.accelX =
            (int16_t)((payloadVector[13] << 8) | payloadVector[14]);
        _tempData.accelY =
            (int16_t)((payloadVector[15] << 8) | payloadVector[16]);
        _tempData.accelZ =
            (int16_t)((payloadVector[17] << 8) | payloadVector[18]);

        _tempData.tag_id = "";
        if (payloadVector.size() >= 31) {
          for (int i = 0; i < 6; i++) {
            char buf[3];
            sprintf(buf, "%02X", payloadVector[25 + i]);
            _tempData.tag_id += String(buf);
          }
        }
        _tempData.uuid = "EA01";

        DVL_PRINTF("[PaPeR] Parsed -> Temp: %.2f C, Hum: %.2f %%, Bat: "
                      "%d%%, D: %d, M: %d, X: %.0f, Y: %.0f, Z: %.0f\n",
                      _tempData.temperature, _tempData.humidity, battPct,
                      _tempData.door, _tempData.motion, _tempData.accelX,
                      _tempData.accelY, _tempData.accelZ);
        DVL_PRINTF("   >>> TagID: %s | UUID: %s\n", _tempData.tag_id.c_str(),
                      _tempData.uuid.c_str());

        _tempData.rawHex = "";
        for (size_t i = 0; i < payloadVector.size(); i++) {
          char output[3];
          sprintf(output, "%02X", payloadVector[i]);
          _tempData.rawHex += String(output);
        }

        _tempData.valid = true;
        _tempData.name = "PaPeR";
        String cleanMac = macAddress.c_str();
        cleanMac.replace(":", "");
        _tempData.mac = cleanMac;
        _tempData.rssi = advertisedDevice->getRSSI();
        _tempData.lastUpdate = millis();

        _tempDataList.push_back(_tempData);
        _foundDevice = true;
      }

      DVL_PRINTLN("------------------------------------------------");
    }

    if (advertisedDevice->haveName() &&
        advertisedDevice->getName().rfind("L02", 0) == 0) {
      DVL_PRINT("BLE: L02S device found! Name: ");
      DVL_PRINTLN(advertisedDevice->getName().c_str());
      DVL_PRINT("RSSI: ");
      DVL_PRINTLN(advertisedDevice->getRSSI());

      MokoSensorData _tempData;
      _tempData.valid = true;
      _tempData.name = advertisedDevice->getName().c_str();
      String cleanMac = advertisedDevice->getAddress().toString().c_str();
      cleanMac.replace(":", "");
      _tempData.mac = cleanMac;
      _tempData.rssi = advertisedDevice->getRSSI();
      _tempData.lastUpdate = millis();

      if (advertisedDevice->haveManufacturerData()) {
        std::string data = advertisedDevice->getManufacturerData();
        String hexData = "";
        for (int i = 0; i < data.length(); i++) {
          char output[3];
          sprintf(output, "%02X", (unsigned char)data[i]);
          hexData += String(output);
        }
        _tempData.rawHex = hexData;
        DVL_PRINT("Raw Hex Data: ");
        DVL_PRINTLN(hexData);

        if (data.length() >= 7) {
          uint8_t batt = (uint8_t)data[2];
          _tempData.batteryLevel =
              map(batt, 0, 100, 0, 3000);

          int16_t tempRaw = ((uint8_t)data[3] << 8) | (uint8_t)data[4];
          _tempData.temperature = tempRaw / 100.0;

          uint16_t humRaw = ((uint8_t)data[5] << 8) | (uint8_t)data[6];
          _tempData.humidity = humRaw / 100.0;

          DVL_PRINTF("Parsed -> Temp: %.2f C, Hum: %.2f %%, Batt: %d %%\n",
                        _tempData.temperature, _tempData.humidity, (int)batt);
        }

        _tempDataList.push_back(_tempData);
        _foundDevice = true;
      }
    }
  }
};

BLEMokoScanner::BLEMokoScanner() {
  newDataAvailable = false;
  lastScanTime = 0;
  pBLEScan = nullptr;
  _initialized = false;
}

void BLEMokoScanner::begin() {
  if (_initialized) {
    return;
  }
  DVL_PRINTLN("Inicializando NimBLE...");
  NimBLEDevice::init("LILY-GO-BLE");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(
      true); // Active scan to get Scan Response (Data might be there)
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->setDuplicateFilter(true); // Filter duplicates in a single scan session
  _initialized = true;
}

void BLEMokoScanner::loop() {
  if (!_initialized) {
    begin();
  }

  unsigned long now = millis();
  if (now - lastScanTime > SCAN_INTERVAL) {
    lastScanTime = now;
    DVL_PRINTLN("Iniciando escaneo NimBLE...");
    _foundDevice = false; // Reset for this scan
    _tempDataList.clear(); // Limpiar la lista de dispisitivos detectados en iteraciones previas

    // Explicitly pass 'false' to ensure we pick the synchronous overload
    // returning results start(duration, is_continue)
    if (pBLEScan != nullptr) {
      DVL_PRINTLN(">> pBLEScan->start(5000, false)...");
      if (pBLEScan->start(
              SCAN_DURATION * 1000,
              false)) { // NimBLE uses ms usually, but check overloaded version
        // If start returns true (async started), we wait
        while (pBLEScan->isScanning()) {
          delay(100);
#ifdef ESP_TASK_WDT_len
          esp_task_wdt_reset();
#endif
        }
      } else {
        DVL_PRINTLN("Fallo al iniciar pBLEScan->start()");
      }

      NimBLEScanResults results = pBLEScan->getResults();
      DVL_PRINT("Dispositivos encontrados: ");
      DVL_PRINTLN(results.getCount());
    } else {
      DVL_PRINTLN("Error: pBLEScan es nulo");
      return;
    }

    if (_foundDevice && _tempDataList.size() > 0) {
      latestData = _tempDataList;
      newDataAvailable = true;
      DVL_PRINTLN("Datos de sensor actualizados (" + String(latestData.size()) + " dispositivos).");
    }

    pBLEScan->clearResults();
  }
}

std::vector<MokoSensorData> BLEMokoScanner::getLatestData() {
  newDataAvailable = false; // Clear flag on read
  return latestData;
}

bool BLEMokoScanner::hasNewData() { return newDataAvailable; }
