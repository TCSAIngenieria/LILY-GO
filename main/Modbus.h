#ifndef MODBUS_H
#define MODBUS_H

#include <Arduino.h>

#define MODBUS_MAX_FRAMES 5

struct ModbusFrameCfg {
  uint8_t id;
  uint8_t func;
  uint16_t addr;
  uint16_t qty;
  bool used;
};

void modbus_begin(Stream* port);
void modbus_set_enabled(bool en);
bool modbus_get_enabled();
bool modbus_set_frame(uint8_t idx, uint8_t id, uint8_t func, uint16_t addr, uint16_t qty);
bool modbus_clear_frame(uint8_t idx);
void modbus_save_to_prefs();
void modbus_load_from_prefs();
void modbus_print_frames();
void modbus_loop();

#endif