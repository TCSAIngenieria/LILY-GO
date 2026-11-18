#include "Modbus.h"
#include "Debug.h"
#include <Preferences.h>
#include <time.h>

static Preferences prefs;
static Stream* mbPort = nullptr;

static bool modbus_enabled = false;
static ModbusFrameCfg frames[MODBUS_MAX_FRAMES];

// Guardar la respuesta completa y los bytes de datos extraídos.
String modbus_lastValues[MODBUS_MAX_FRAMES];
String modbus_lastData[MODBUS_MAX_FRAMES];
String modbus_lastResponseDate[MODBUS_MAX_FRAMES];

static void modbus_clear_last_value(uint8_t i) {
  modbus_lastValues[i] = "";
  modbus_lastData[i] = "";
  modbus_lastResponseDate[i] = "";
}

// Descarta solo las respuestas en RAM; las consultas y la cola flash se conservan.
void modbus_clear_last_values() {
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    modbus_clear_last_value(i);
  }
}

// Polling
static unsigned long lastPoll = 0;
static const unsigned long pollIntervalMs = 2000;
static uint8_t pollIndex = 0;

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

// Unica construccion de la trama para transmitir y publicar la misma consulta.
static void modbus_build_request(const ModbusFrameCfg& f, uint8_t* frame) {
  frame[0] = f.id;
  frame[1] = f.func;
  frame[2] = uint8_t(f.addr >> 8);
  frame[3] = uint8_t(f.addr & 0xFF);
  frame[4] = uint8_t(f.qty >> 8);
  frame[5] = uint8_t(f.qty & 0xFF);
  uint16_t crc = modbus_crc16(frame, 6);
  frame[6] = uint8_t(crc & 0xFF);
  frame[7] = uint8_t(crc >> 8);
}

static String modbus_response_date(time_t receivedAt) {
  struct tm utc;
  if (!gmtime_r(&receivedAt, &utc) || utc.tm_year < 124 || utc.tm_year > 199) {
    return "";  // No asignar fechas de 1970 a lecturas previas a sincronizar.
  }
  char date[20];
  strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &utc);
  return String(date);
}

String modbus_get_request_hex(uint8_t idx) {
  if (idx < 1 || idx > MODBUS_MAX_FRAMES || !frames[idx - 1].used) return "";
  uint8_t request[8];
  modbus_build_request(frames[idx - 1], request);
  char hex[24];
  snprintf(hex, sizeof(hex), "%02X %02X %02X %02X %02X %02X %02X %02X",
           request[0], request[1], request[2], request[3], request[4],
           request[5], request[6], request[7]);
  return String(hex);
}

static String modbus_bytes_to_hex(const uint8_t* data, size_t len) {
  String result = "";
  result.reserve(len * 2);

  for (size_t i = 0; i < len; i++) {
    char hexByte[3];
    snprintf(hexByte, sizeof(hexByte), "%02X", data[i]);
    result += hexByte;
  }

  return result;
}

// Extrae solamente el campo de datos de respuestas Modbus RTU estándar.
// Las funciones 01-04 informan su longitud en el tercer byte. Las funciones
// 05-06 devuelven como dato el valor escrito. En excepciones, CRC inválido o
// funciones sin un campo de valor inequívoco se devuelve una cadena vacía.
static String modbus_extract_data(const uint8_t* response, size_t len,
                                  const ModbusFrameCfg& request) {
  if (len < 5 || response[0] != request.id) return "";

  uint8_t responseFunc = response[1];
  if ((responseFunc & 0x80) != 0 || responseFunc != request.func) return "";

  uint16_t receivedCrc = uint16_t(response[len - 2]) |
                         (uint16_t(response[len - 1]) << 8);
  if (modbus_crc16(response, len - 2) != receivedCrc) return "";

  if (responseFunc >= 0x01 && responseFunc <= 0x04) {
    size_t byteCount = response[2];
    if (byteCount == 0 || len != byteCount + 5) return "";

    size_t expectedBytes = responseFunc <= 0x02
                               ? (size_t(request.qty) + 7) / 8
                               : size_t(request.qty) * 2;
    if (byteCount != expectedBytes) return "";

    return modbus_bytes_to_hex(response + 3, byteCount);
  }

  if ((responseFunc == 0x05 || responseFunc == 0x06) && len == 8) {
    uint8_t expected[8];
    modbus_build_request(request, expected);
    for (size_t i = 0; i < 6; i++) {
      if (response[i] != expected[i]) return "";
    }
    return modbus_bytes_to_hex(response + 4, 2);
  }

  return "";
}

void modbus_begin(Stream* port) {
  mbPort = port;
  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    frames[i] = { 0, 0, 0, 0, false };
    modbus_clear_last_value(i);
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

bool modbus_set_frame(uint8_t idx, uint8_t id, uint8_t func, uint16_t addr,
                      uint16_t qty) {
  if (idx < 1 || idx > MODBUS_MAX_FRAMES) return false;
  uint8_t i = idx - 1;
  frames[i].id = id;
  frames[i].func = func;
  frames[i].addr = addr;
  frames[i].qty = qty;
  frames[i].used = true;
  modbus_clear_last_value(i);
  modbus_save_to_prefs();
  return true;
}

bool modbus_get_frame(uint8_t idx, ModbusFrameCfg &frame) {
  if (idx < 1 || idx > MODBUS_MAX_FRAMES) return false;
  frame = frames[idx - 1];
  return true;
}

bool modbus_clear_frame(uint8_t idx) {
  prefs.begin("modbus", false);
  if (idx == 0) {
    for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
      frames[i] = { 0, 0, 0, 0, false };
      modbus_clear_last_value(i);
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
  modbus_clear_last_value(i);
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
    uint8_t blob[8] = { 0 };
    size_t n = prefs.getBytes(key, blob, sizeof(blob));
    // Acepta tanto la configuración actual de 7 bytes como la anterior de 8.
    if (n == 7 || n == 8) {
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
      DVL_PRINTF("  #%d: ID=0x%02X FUNC=0x%02X ADDR=0x%04X "
                 "QTY=0x%04X\n",
                 i + 1, frames[i].id, frames[i].func, frames[i].addr,
                 frames[i].qty);
    } else {
      DVL_PRINTF("  #%d: <vacío>\n", i + 1);
    }
  }
}

String modbus_get_frames_summary() {
  String summary = "";

  for (int i = 0; i < MODBUS_MAX_FRAMES; i++) {
    if (!frames[i].used) continue;

    char frameSummary[96];
    snprintf(frameSummary, sizeof(frameSummary),
             "MODBUS#%d ID=0x%02X FUNC=0x%02X ADDR=0x%04X "
             "QTY=0x%04X",
             i + 1, frames[i].id, frames[i].func, frames[i].addr,
             frames[i].qty);

    if (summary.length() > 0) summary += " | ";
    summary += frameSummary;
  }

  return summary.length() > 0 ? "[OK] " + summary
                              : "[OK] Sin tramas Modbus configuradas";
}

// 👇 ahora esta función está definida antes de modbus_loop
static bool modbus_send_frame(const ModbusFrameCfg& f, uint8_t frameIndex) {
  if (!mbPort) return false;
  uint8_t frame[8];
  modbus_build_request(f, frame);

  mbPort->write(frame, sizeof(frame));
  mbPort->flush();

  DVL_PRINT("[MODBUS] TX: ");
  for (int i = 0; i < 8; i++) DVL_PRINTF("%02X ", frame[i]);
  DVL_PRINTLN("");

  // Leer respuesta rápida
  unsigned long t0 = millis();
  uint8_t buffer[64];
  int len = 0;
  time_t receivedAt = 0;

  while (millis() - t0 < 200) {
    while (mbPort->available() && len < sizeof(buffer)) {
      int c = mbPort->read();
      if (c >= 0) {
        buffer[len++] = (uint8_t)c;
        time(&receivedAt);
      }
    }
  }

  if (len > 0) {
    DVL_PRINT("[MODBUS] RX: ");
    String hexResp = "";
    hexResp.reserve((len * 3) - 1);
    for (int i = 0; i < len; i++) {
      DVL_PRINTF("%02X ", buffer[i]);  // imprime en mayúsculas
      if (i > 0) hexResp += ' ';
      char tmp[3];
      sprintf(tmp, "%02X", buffer[i]);
      hexResp += tmp;  // guarda en string global
    }
    DVL_PRINTLN("");
    String data = modbus_extract_data(buffer, len, f);
    if (data.length() > 0) {
      const bool changed = data != modbus_lastData[frameIndex];
      modbus_lastValues[frameIndex] = hexResp;
      modbus_lastData[frameIndex] = data;
      modbus_lastResponseDate[frameIndex] = modbus_response_date(receivedAt);
      return changed;
    } else {
      DVL_PRINTLN("[MODBUS] Respuesta invalida para esta consulta; se conserva la ultima valida.");
    }
  } else {
    DVL_PRINTLN("[MODBUS] Sin respuesta; se conserva la ultima valida.");
  }
  return false;
}

uint8_t modbus_loop() {
  if (!modbus_enabled) return 0;
  if (!mbPort) return 0;

  unsigned long now = millis();
  if (now - lastPoll < pollIntervalMs) return 0;
  lastPoll = now;

  for (int k = 0; k < MODBUS_MAX_FRAMES; k++) {
    uint8_t i = (pollIndex + k) % MODBUS_MAX_FRAMES;
    if (frames[i].used) {
      const bool changed = modbus_send_frame(frames[i], i);
      pollIndex = (i + 1) % MODBUS_MAX_FRAMES;
      return changed ? i + 1 : 0;
    }
  }
  return 0;
}
