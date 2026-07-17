#ifndef MOKO_DOOR_H
#define MOKO_DOOR_H

#include "BLE_MOKO.h"

bool mokoDoorTryParseAdvertisement(const NimBLEAdvertisedDevice *device,
                                   MokoSensorData &data);

#endif
