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
        // Also update _tempData for this device so we can see it in normal flow
        // if needed
        _tempData.rawHex = hexData;
        _tempData.valid = true;
        _tempData.name = "TARGET_DEVICE";
        _tempData.mac = macAddress.c_str();
        _tempData.rssi = advertisedDevice->getRSSI();
        _tempData.lastUpdate = millis();
        // (La captura de target manual no se guarda aquí si se procesa abajo)
      } else {
        DVL_PRINTLN("No Manufacturer Data");
      }

      if (advertisedDevice->haveServiceUUID()) {
        DVL_PRINT("Service UUID: ");
        DVL_PRINTLN(advertisedDevice->getServiceUUID().toString().c_str());
      }

      // DUMP FULL PAYLOAD
      // getPayload() returns a const std::vector<uint8_t>* or reference in some
      // versions, but error says it returns const std::vector<unsigned char>.
      // Let's copy it or reference it.
      // Note: In some NimBLE versions getPayload() returns a pointer to vector,
      // in others the vector/array directly. The error "cannot convert 'const
      // std::vector<unsigned char>' to 'uint8_t*'" suggests it returns the
      // vector itself? Wait, "advertisedDevice->getPayload()" returning a
      // vector means we assignment should range based loop or reference.
      // Actually, looking at common NimBLE-Arduino: it returns `uint8_t*` OR
      // `std::string` OR `std::vector` depending on fork. The error says:
      // `cannot convert 'const std::vector<unsigned char>' to 'uint8_t*'` So
      // `advertisedDevice->getPayload()` IS a `const std::vector<unsigned
      // char>`. (Wait, usually it returns a pointer to it? Ah, maybe the error
      // message implies the return type is that object. Let's try `const
      // std::vector<uint8_t>& payload = advertisedDevice->getPayload();` NO, if
      // it returned a pointer the error would be different. Actually common
      // NimBLE: `uint8_t* getPayload()` is NOT standard. `std::string
      // getPayload()` or `std::vector<uint8_t> getPayload()`. Let's assume it
      // returns `std::vector<uint8_t>`.

      const std::vector<uint8_t> &payloadVector =
          advertisedDevice->getPayload();

      // PARSEO ESPECIFICO PARA MOKO L02S / PaPeR (Service Data 0xEA01)
      // Offsets encontrados: Temp[19-20], Hum[21-22], Batt[23-24]
      // Motion? Index 27 (byte 28) seems to be 0x01 in example.
      if (payloadVector.size() >= 28) {
        MokoSensorData _tempData; // Local object to store this device's data

        // Temp (Big Endian)
        int16_t tempRaw =
            ((uint16_t)payloadVector[19] << 8) | payloadVector[20];
        _tempData.temperature = tempRaw / 10.0;

        // Hum (Big Endian)
        uint16_t humRaw =
            ((uint16_t)payloadVector[21] << 8) | payloadVector[22];
        _tempData.humidity = humRaw / 10.0;

        // Batt (mV) (Big Endian)
        uint16_t battMv =
            ((uint16_t)payloadVector[23] << 8) | payloadVector[24];
        int battPct = map(battMv, 2000, 3100, 0, 100);
        battPct = constrain(battPct, 0, 100);
        _tempData.batteryLevel = battPct;

        // Sensor Status (Byte 8)
        uint8_t statusByte = payloadVector[8];
        _tempData.motion = (statusByte >> 1) & 0x01;
        _tempData.door = (statusByte >> 0) & 0x01;

        // Accelerometer (Big Endian) - Shifted -1 from table
        _tempData.accelX =
            (int16_t)((payloadVector[13] << 8) | payloadVector[14]);
        _tempData.accelY =
            (int16_t)((payloadVector[15] << 8) | payloadVector[16]);
        _tempData.accelZ =
            (int16_t)((payloadVector[17] << 8) | payloadVector[18]);

        // Tag ID (6 bytes starting at 25)
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

        // MOKO L02S Parsing Logic (Tentativo)
        // Se asume el formato: [0-1] CompanyID, [u] Battery, [u][u] Temp,
        // [u][u] Hum Temp y Hum suelen ser BigEndian y div 100. Validar
        // longitud minima (ej. 7 bytes)
        if (data.length() >= 7) {
          // Battery (Byte 2)
          uint8_t batt = (uint8_t)data[2];
          _tempData.batteryLevel =
              map(batt, 0, 100, 0,
                  3000); // Guardamos o convertimos segun necesidad, aqui raw %

          // Temperature (Bytes 3-4) - Big Endian
          int16_t tempRaw = ((uint8_t)data[3] << 8) | (uint8_t)data[4];
          _tempData.temperature = tempRaw / 100.0;

          // Humidity (Bytes 5-6) - Big Endian
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
