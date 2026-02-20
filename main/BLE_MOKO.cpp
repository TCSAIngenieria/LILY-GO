#include "BLE_MOKO.h"

// Usamos un vector temporal estático para el callback, ya que es una clase
// separada
static std::vector<MokoSensorData> _tempDeviceList;

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {

    // Check for specific Name (Client Name)
    String devName = "";
    if (advertisedDevice->haveName()) {
      devName = advertisedDevice->getName().c_str();
    }

    // Condicion: Nombre == "PaPeR" o empieza con "L02"
    bool isPaper = (devName == "PaPeR");
    bool isL02S = devName.startsWith("L02");

    // if (!isPaper && !isL02S) {
    //   return; // No es un dispositivo de interes
    // }

    Serial.print("BLE Found: ");
    Serial.print(devName);
    Serial.print(" MAC: ");
    Serial.print(advertisedDevice->getAddress().toString().c_str());
    Serial.print(" RSSI: ");
    Serial.println(advertisedDevice->getRSSI());

    std::string macAddress = advertisedDevice->getAddress().toString();
    MokoSensorData currentData;
    currentData.valid = false;

    /*Agregado para pruebas*/
    bool isMAC = (macAddress.c_str() == "CC:03:20:3A:4D:A9");

    /*Agregado para pruebas*/

    // --- LOGICA PARA PaPeR ---
    if (isPaper || isMAC) {
      Serial.println(">>> TARGET DEVICE FOUND (PaPeR) <<<");

      if (advertisedDevice->haveManufacturerData()) {
        // ... logica de manufacturer data si fuera necesaria para PaPeR ...
      }

      if (advertisedDevice->haveServiceUUID()) {
        // ... logica UUID ...
      }

      const std::vector<uint8_t> &payloadVector =
          advertisedDevice->getPayload();

      // PARSEO ESPECIFICO PARA MOKO PaPeR (Service Data 0xEA01)
      if (payloadVector.size() >= 28) {
        // Temp (Big Endian)
        int16_t tempRaw =
            ((uint16_t)payloadVector[19] << 8) | payloadVector[20];
        currentData.temperature = tempRaw / 10.0;

        // Hum (Big Endian)
        uint16_t humRaw =
            ((uint16_t)payloadVector[21] << 8) | payloadVector[22];
        currentData.humidity = humRaw / 10.0;

        // Batt (mV) (Big Endian)
        uint16_t battMv =
            ((uint16_t)payloadVector[23] << 8) | payloadVector[24];
        int battPct = map(battMv, 2000, 3100, 0, 100);
        battPct = constrain(battPct, 0, 100);
        currentData.batteryLevel = battPct;

        // Sensor Status (Byte 8)
        uint8_t statusByte = payloadVector[8];
        currentData.motion = (statusByte >> 1) & 0x01;
        currentData.door = (statusByte >> 0) & 0x01;

        // Accelerometer
        currentData.accelX =
            (int16_t)((payloadVector[13] << 8) | payloadVector[14]);
        currentData.accelY =
            (int16_t)((payloadVector[15] << 8) | payloadVector[16]);
        currentData.accelZ =
            (int16_t)((payloadVector[17] << 8) | payloadVector[18]);

        // Tag ID
        currentData.tag_id = "";
        if (payloadVector.size() >= 31) {
          for (int i = 0; i < 6; i++) {
            char buf[3];
            sprintf(buf, "%02X", payloadVector[25 + i]);
            currentData.tag_id += String(buf);
          }
        }
        currentData.uuid = "EA01";

        currentData.valid = true;
        currentData.name = "PaPeR";
        String cleanMac = macAddress.c_str();
        cleanMac.replace(":", "");
        currentData.mac = cleanMac;
        currentData.rssi = advertisedDevice->getRSSI();
        currentData.lastUpdate = millis();

        // Agregar a la lista temporal
        _tempDeviceList.push_back(currentData);
        Serial.println(" -> Agregado a cola de procesamiento.");
      }
    }

    // --- LOGICA PARA L02S ---
    else if (isL02S) {
      Serial.println(">>> L02S DEVICE FOUND <<<");

      if (advertisedDevice->haveManufacturerData()) {
        std::string data = advertisedDevice->getManufacturerData();

        if (data.length() >= 7) {
          // Battery
          uint8_t batt = (uint8_t)data[2];
          currentData.batteryLevel =
              map(batt, 0, 100, 0, 3000); // Raw mapping as per original code

          // Temperature
          int16_t tempRaw = ((uint8_t)data[3] << 8) | (uint8_t)data[4];
          currentData.temperature = tempRaw / 100.0;

          // Humidity
          uint16_t humRaw = ((uint8_t)data[5] << 8) | (uint8_t)data[6];
          currentData.humidity = humRaw / 100.0;

          currentData.valid = true;
          currentData.name = devName;
          String cleanMac = macAddress.c_str();
          cleanMac.replace(":", "");
          currentData.mac = cleanMac;
          currentData.rssi = advertisedDevice->getRSSI();
          currentData.lastUpdate = millis();

          // Agregar a la lista temporal
          _tempDeviceList.push_back(currentData);
          Serial.println(" -> Agregado a cola de procesamiento.");
        }
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
  Serial.println("Inicializando NimBLE...");
  NimBLEDevice::init("LILY-GO-BLE");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->setDuplicateFilter(false);
  _initialized = true;
}

void BLEMokoScanner::loop() {
  if (!_initialized) {
    begin();
  }

  unsigned long now = millis();
  if (now - lastScanTime > SCAN_INTERVAL) {
    lastScanTime = now;
    Serial.println("Iniciando escaneo NimBLE...");

    // Limpiamos la lista temporal antes de escanear
    _tempDeviceList.clear();
    // Limpiamos la cola publica anterior si no fue consumida
    deviceQueue.clear();
    newDataAvailable = false;

    if (pBLEScan != nullptr) {
      Serial.println(">> pBLEScan->start(5000, false)...");
      if (pBLEScan->start(SCAN_DURATION * 1000, false)) {
        while (pBLEScan->isScanning()) {
          delay(100);
#ifdef ESP_TASK_WDT_len
          esp_task_wdt_reset();
#endif
        }
      } else {
        Serial.println("Fallo al iniciar pBLEScan->start()");
      }

      NimBLEScanResults results = pBLEScan->getResults();
      Serial.print("Dispositivos encontrados (Total Raw): ");
      Serial.println(results.getCount());

      // Transferir lista temporal a pública
      if (_tempDeviceList.size() > 0) {
        for (auto &d : _tempDeviceList) {
          deviceQueue.push_back(d);
        }
        newDataAvailable = true;
        Serial.print("Dispositivos VALIDOS en cola: ");
        Serial.println(deviceQueue.size());
      }

    } else {
      Serial.println("Error: pBLEScan es nulo");
      return;
    }

    pBLEScan->clearResults();
  }
}

// Devuelve el siguiente dispositivo de la cola y lo remueve
bool BLEMokoScanner::getNextDevice(MokoSensorData *data) {
  if (deviceQueue.empty()) {
    newDataAvailable = false;
    return false;
  }

  *data = deviceQueue.front();
  deviceQueue.erase(deviceQueue.begin());

  if (deviceQueue.empty()) {
    newDataAvailable = false;
  }
  return true;
}

bool BLEMokoScanner::hasNewData() { return newDataAvailable; }
