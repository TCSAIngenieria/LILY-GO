#include "Modbus.h"
#include "Debug.h"
#include <Preferences.h>

static Preferences prefs;
static Stream* mbPort = nullptr;

static bool modbus_enabled = false;
static ModbusFrameCfg frames[MODBUS_MAX_FRAMES];

// Guardar últimas respuestas (como texto HEX por ahora)
String modbus_lastValues[MODBUS_MAX_FRAMES];

// Polling
static unsigned long lastPoll = 0;
static const unsigned long pollIntervalMs = 5000;
static uint8_t pollIndex = 0;  // 0..4

// CRC16 Modbus (poly 0xA001, init 0xFFFF)
static uint16_t modbus_crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void modbus_begin(Stream* port) {
  mbPort = port;
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    frames[i] = { 0, 0, 0, 0, false };
    modbus_lastValues[i] = "";
  }
  modbus_load_from_prefs();
}

void modbus_set_enabled(bool en) {
  modbus_enabled = en;
  prefs.begin("modbus", false);
  prefs.putUChar("enabled", en ? 1 : 0);
  prefs.end();
}

bool modbus_get_enabled() {
  return modbus_enabled;
}

bool modbus_set_frame(uint8_t idx, uint8_t id, uint8_t func, uint16_t addr, uint16_t qty) {
  if (idx < 1 || idx > MODBUS_MAX_FRAMES) return false;
  uint8_t i = idx - 1;
  frames[i].id = id;
  frames[i].func = func;
  frames[i].addr = addr;
  frames[i].qty = qty;
  frames[i].used = true;
  modbus_save_to_prefs();
  return true;
}

bool modbus_clear_frame(uint8_t idx) {
  prefs.begin("modbus", false);
  if (idx == 0) {
    for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
      frames[i] = { 0, 0, 0, 0, false };
      char key[16];
      sprintf(key, "f%d", i + 1);
      prefs.remove(key);
    }
    prefs.end();
    return true;
  }
  if (idx < 1 || idx > MODBUS_MAX_FRAMES) {
    prefs.end();
    return false;
  }
  uint8_t i = idx - 1;
  frames[i] = { 0, 0, 0, 0, false };
  char key[16];
  sprintf(key, "f%d", idx);
  prefs.remove(key);
  prefs.end();
  return true;
}

void modbus_save_to_prefs() {
  prefs.begin("modbus", false);
  prefs.putUChar("enabled", modbus_enabled ? 1 : 0);
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    char key[16];
    sprintf(key, "f%d", i + 1);
    uint8_t blob[7] = {
      frames[i].id,
      frames[i].func,
      (uint8_t)(frames[i].addr >> 8),
      (uint8_t)(frames[i].addr & 0xFF),
      (uint8_t)(frames[i].qty >> 8),
      (uint8_t)(frames[i].qty & 0xFF),
      frames[i].used ? 1 : 0
    };
    prefs.putBytes(key, blob, sizeof(blob));
  }
  prefs.end();
}

void modbus_load_from_prefs() {
  prefs.begin("modbus", true);
  modbus_enabled = prefs.getUChar("enabled", 0) == 1;
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    char key[16];
    sprintf(key, "f%d", i + 1);
    uint8_t blob[7] = { 0 };
    size_t n = prefs.getBytes(key, blob, sizeof(blob));
    if (n == sizeof(blob)) {
      frames[i].id = blob[0];
      frames[i].func = blob[1];
      frames[i].addr = (uint16_t(blob[2]) << 8) | blob[3];
      frames[i].qty = (uint16_t(blob[4]) << 8) | blob[5];
      frames[i].used = (blob[6] != 0);
    } else {
      frames[i] = { 0, 0, 0, 0, false };
    }
  }
  prefs.end();
}

void modbus_print_frames() {
  DVL_PRINTLN("[MODBUS] Frames configurados:");
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    if (frames[i].used) {
      DVL_PRINTF("  #%d: ID=0x%02X FUNC=0x%02X ADDR=0x%04X QTY=0x%04X\n",
                    i + 1, frames[i].id, frames[i].func, frames[i].addr, frames[i].qty);
    } else {
      DVL_PRINTF("  #%d: <vacío>\n", i + 1);
    }
  }
}

// 👇 ahora esta función está definida antes de modbus_loop
static void modbus_send_frame(const ModbusFrameCfg& f, uint8_t frameIndex) {
  if (!mbPort) return;
  uint8_t payload[6] = {
    f.id,
    f.func,
    (uint8_t)(f.addr >> 8),
    (uint8_t)(f.addr & 0xFF),
    (uint8_t)(f.qty >> 8),
    (uint8_t)(f.qty & 0xFF)
  };

  uint8_t frame[8] = {
    payload[0], payload[1], payload[2],
    payload[3], payload[4], payload[5],
    0, 0
  };

  uint16_t crc = modbus_crc16(payload, 6);
  frame[6] = (uint8_t)(crc & 0xFF);  // LOW
  frame[7] = (uint8_t)(crc >> 8);    // HIGH

  mbPort->write(frame, sizeof(frame));
  mbPort->flush();

  DVL_PRINT("[MODBUS] TX: ");
  for (int i = 0; i < 8; i++) DVL_PRINTF("%02X ", frame[i]);
  DVL_PRINTLN("");

  // Leer respuesta rápida
  unsigned long t0 = millis();
  uint8_t buffer[64];
  int len = 0;

  while (millis() - t0 < 200) {
    while (mbPort->available() && len < sizeof(buffer)) {
      int c = mbPort->read();
      if (c >= 0) buffer[len++] = (uint8_t)c;
    }
  }

  if (len > 0) {
    DVL_PRINT("[MODBUS] RX: ");
    String hexResp = "";
    for (int i = 0; i < len; i++) {
      DVL_PRINTF("%02X ", buffer[i]);  // imprime en mayúsculas
      char tmp[4];
      sprintf(tmp, "%02X ", buffer[i]);
      hexResp += tmp;  // guarda en string global
    }
    DVL_PRINTLN("");
    modbus_lastValues[frameIndex] = hexResp;  // guarda último RX para JSON
  } else {
    modbus_lastValues[frameIndex] = "";
  }
}

void modbus_loop() {
  if (!modbus_enabled) return;
  if (!mbPort) return;

  unsigned long now = millis();
  if (now - lastPoll < pollIntervalMs) return;
  lastPoll = now;

  for (int k = 0; k < MODBUS_MAX_FRAMES; k++) {
    uint8_t i = (pollIndex + k) % MODBUS_MAX_FRAMES;
    if (frames[i].used) {
      modbus_send_frame(frames[i], i);  // ✅ ahora ya está definida arriba
      pollIndex = (i + 1) % MODBUS_MAX_FRAMES;
      break;
    }
  }
}