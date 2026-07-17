#include "MOKO_Door.h"

#include "Debug.h"

static const uint16_t MOKO_DOOR_MANUFACTURER_ID = 0x620A;
static const size_t MOKO_DOOR_IBEACON_LENGTH = 25;

static String cleanMacAddress(const std::string &macAddress) {
  String cleanMac = String(macAddress.c_str());
  cleanMac.replace(":", "");
  return cleanMac;
}

static uint16_t readU16BE(const std::string &data, size_t index) {
  return ((uint16_t)(uint8_t)data[index] << 8) |
         (uint8_t)data[index + 1];
}

static int batteryPercentFromMilliVolts(uint16_t battMv) {
  int battPct = map(battMv, 2000, 3600, 0, 100);
  return constrain(battPct, 0, 100);
}

bool mokoDoorTryParseAdvertisement(const NimBLEAdvertisedDevice *device,
                                   MokoSensorData &data) {
  if (!device->haveManufacturerData()) {
    return false;
  }

  std::string mfgData = device->getManufacturerData();
  if (mfgData.length() != MOKO_DOOR_IBEACON_LENGTH) {
    return false;
  }

  uint16_t manufacturerId = ((uint16_t)(uint8_t)mfgData[1] << 8) |
                            (uint8_t)mfgData[0];
  if (manufacturerId != MOKO_DOOR_MANUFACTURER_ID ||
      (uint8_t)mfgData[2] != 0x02 || (uint8_t)mfgData[3] != 0x15) {
    return false;
  }

  uint8_t status = (uint8_t)mfgData[4];
  uint8_t doorRaw = (status >> 3) & 0x01;
  uint8_t pirRaw = status & 0x01;
  uint16_t batteryMv = readU16BE(mfgData, 18);

  data = MokoSensorData();
  data.valid = true;
  data.name = device->haveName() ? String(device->getName().c_str())
                                 : "MkiBeacon";
  data.mac = cleanMacAddress(device->getAddress().toString());
  data.rssi = device->getRSSI();
  data.lastUpdate = millis();
  data.frameType = 0x81;
  data.uuid = "MOKO_DOOR";
  data.manufacturerId = manufacturerId;

  data.door = doorRaw;
  data.doorOpen = doorRaw == 1;
  data.hasDoorStatus = true;
  data.hasDoorOpen = true;

  data.motion = pirRaw;
  data.pirRaw = pirRaw;
  data.hasPirRaw = true;
  data.pirValid = true;
  data.hasMotionStatus = true;

  if (batteryMv >= 2000 && batteryMv <= 5000) {
    data.batteryMv = batteryMv;
    data.batteryLevel = batteryPercentFromMilliVolts(batteryMv);
    data.batteryValid = true;
  }

  data.ibeaconUuid = "";
  for (int i = 4; i <= 19; i++) {
    char buf[3];
    sprintf(buf, "%02X", (uint8_t)mfgData[i]);
    data.ibeaconUuid += String(buf);
  }
  data.major = readU16BE(mfgData, 20);
  data.minor = readU16BE(mfgData, 22);
  data.rssi1m = (int8_t)mfgData[24];

  DVL_PRINTF(
      "  MOKO Door -> door_open=%s, pir_motion=%s, battery=%u mV\n",
      data.doorOpen ? "true" : "false", data.motion == 1 ? "true" : "false",
      data.batteryMv);
  return true;
}
