#ifndef MODBUS_H
#define MODBUS_H

#include <Arduino.h>

// Máximo de tramas configurables
#define MODBUS_MAX_FRAMES 25

typedef struct {
  uint8_t id;
  uint8_t func;
  uint16_t addr;
  uint16_t qty;
  bool used;
} ModbusFrameCfg;

// API
void modbus_begin(Stream* port);
void modbus_set_enabled(bool en);
bool modbus_get_enabled();

bool modbus_set_frame(uint8_t idx, uint8_t id, uint8_t func, uint16_t addr,
                      uint16_t qty);
bool modbus_clear_frame(uint8_t idx);
bool modbus_get_frame(uint8_t idx, ModbusFrameCfg &frame);
String modbus_get_request_hex(uint8_t idx);

void modbus_save_to_prefs();
void modbus_load_from_prefs();

void modbus_print_frames();
String modbus_get_frames_summary();
// Devuelve el idx (1-25) con datos validos nuevos; 0 si no hubo cambio.
uint8_t modbus_loop();
void modbus_clear_last_values();

extern String modbus_lastValues[MODBUS_MAX_FRAMES];
extern String modbus_lastData[MODBUS_MAX_FRAMES];
extern String modbus_lastResponseDate[MODBUS_MAX_FRAMES];

#endif
