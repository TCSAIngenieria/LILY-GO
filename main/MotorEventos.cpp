#include "MotorEventos.h"
#include "Debug.h"
#include "Fechayhora.h"
#include "Flash.h"
#include "MQTT.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

#ifndef SENSOR_POWER_PIN
#define SENSOR_POWER_PIN 23
#endif

// Sensor de temperatura interno del silicio ESP32
#ifdef __cplusplus
extern "C" float temperatureRead();
#endif

extern String ident;
extern String topic1;
extern PubSubClient mqtt;

// Variables de sensores cableados ya existentes en el firmware
extern String valorStr;          // Última lectura del sensor DS18B20
extern String sensorValues[16];   // Canales S0..S15 de la placa expansora serial

// Estructura y tabla en RAM para almacenar lecturas de múltiples balizas BLE MOKO
#define MAX_BLE_DEVICES 16

struct CachedBleDevice {
  char mac[13];
  float temp;
  float hum;
  float door;
  float mov;
  float bat;
  unsigned long lastSeen;
};

static CachedBleDevice bleTable[MAX_BLE_DEVICES];
static int bleDeviceCount = 0;
static int lastUpdatedBleIndex = -1;

static EventRule eventRules[MAX_EVENTOS];
static DateTimeTrigger dtTriggers[MAX_DT_TRIGGERS];
static Preferences prefEventos;

void actualizarDatosBLE(const String &mac, float temp, float hum, float door, float mov, float bat) {
  if (mac.length() == 0) return;

  String cleanMac = mac;
  cleanMac.replace(":", "");
  cleanMac.replace("-", "");
  cleanMac.toUpperCase();
  cleanMac.trim();

  int foundIdx = -1;
  for (int i = 0; i < bleDeviceCount; i++) {
    if (strcmp(bleTable[i].mac, cleanMac.c_str()) == 0) {
      foundIdx = i;
      break;
    }
  }

  if (foundIdx == -1) {
    if (bleDeviceCount < MAX_BLE_DEVICES) {
      foundIdx = bleDeviceCount++;
    } else {
      // Si la tabla se llena, reutilizar la entrada con timestamp más antiguo
      unsigned long oldestTime = 0xFFFFFFFF;
      foundIdx = 0;
      for (int i = 0; i < bleDeviceCount; i++) {
        if (bleTable[i].lastSeen < oldestTime) {
          oldestTime = bleTable[i].lastSeen;
          foundIdx = i;
        }
      }
    }
    memset(&bleTable[foundIdx], 0, sizeof(CachedBleDevice));
    strncpy(bleTable[foundIdx].mac, cleanMac.c_str(), 12);
    bleTable[foundIdx].mac[12] = '\0';
  }

  if (temp != 0.0f) bleTable[foundIdx].temp = temp;
  if (hum != 0.0f) bleTable[foundIdx].hum = hum;
  bleTable[foundIdx].door = door;
  bleTable[foundIdx].mov = mov;
  if (bat > 0.0f) bleTable[foundIdx].bat = bat;
  bleTable[foundIdx].lastSeen = millis();
  lastUpdatedBleIndex = foundIdx;
}

static int buscarDispositivoBLE(const char *macFilter) {
  if (bleDeviceCount == 0) return -1;

  if (macFilter != nullptr && strlen(macFilter) > 0) {
    String filter = String(macFilter);
    filter.replace(":", "");
    filter.replace("-", "");
    filter.toUpperCase();
    filter.trim();

    // 1. Coincidencia exacta de MAC
    for (int i = 0; i < bleDeviceCount; i++) {
      if (strcasecmp(bleTable[i].mac, filter.c_str()) == 0) {
        return i;
      }
    }

    // 2. Coincidencia de sufijo (últimos N caracteres de la MAC, ej. últimos 4)
    for (int i = 0; i < bleDeviceCount; i++) {
      String devMac = String(bleTable[i].mac);
      if (devMac.endsWith(filter)) {
        return i;
      }
    }

    // Filtro especificado pero no encontrado en caché aún
    return -1;
  }

  // Si no se especificó MAC, retornar el último actualizado o el primero disponible
  if (lastUpdatedBleIndex >= 0 && lastUpdatedBleIndex < bleDeviceCount) {
    return lastUpdatedBleIndex;
  }
  return 0;
}

String eventSourceToString(EventSource src, const char *bleMac) {
  if (src >= EVT_SRC_S0 && src <= EVT_SRC_S15) {
    return "S" + String(src - EVT_SRC_S0);
  }
  if (src >= EVT_SRC_DT0 && src <= EVT_SRC_DT3) {
    return "DT" + String(src - EVT_SRC_DT0);
  }

  String base = "";
  bool isBle = false;

  switch (src) {
    case EVT_SRC_VPRIN:    base = "VPRIN"; break;
    case EVT_SRC_VBACK:    base = "VBACK"; break;
    case EVT_SRC_ADC0:     base = "ADC0"; break;
    case EVT_SRC_ADC1:     base = "ADC1"; break;
    case EVT_SRC_DI1:      base = "DI1"; break;
    case EVT_SRC_DI2:      base = "DI2"; break;
    case EVT_SRC_COMM:     base = "COMM"; break;
    case EVT_SRC_TEMP:     base = "TEMP"; break;
    case EVT_SRC_BLE_DOOR: base = "BLE_DOOR"; isBle = true; break;
    case EVT_SRC_BLE_MOV:  base = "BLE_MOV";  isBle = true; break;
    case EVT_SRC_BLE_TEMP: base = "BLE_TEMP"; isBle = true; break;
    case EVT_SRC_BLE_HUM:  base = "BLE_HUM";  isBle = true; break;
    case EVT_SRC_BLE_BAT:  base = "BLE_BAT";  isBle = true; break;
    case EVT_SRC_TEMP_MCU: base = "TEMP_MCU"; break;
    case EVT_SRC_HEAP:     base = "HEAP"; break;
    case EVT_SRC_BTN:      base = "BTN"; break;
    default:               return "NONE";
  }

  if (isBle && bleMac != nullptr && strlen(bleMac) > 0) {
    return base + ":" + String(bleMac);
  }
  return base;
}

EventSource stringToEventSource(const String &str, char *outMac) {
  String s = str;
  s.toUpperCase();
  s.trim();

  if (outMac != nullptr) {
    outMac[0] = '\0';
  }

  // Detección de especificación de MAC vía "VARIABLE:MAC"
  int colonIdx = s.indexOf(':');
  if (colonIdx != -1) {
    String macPart = s.substring(colonIdx + 1);
    macPart.replace(":", "");
    macPart.replace("-", "");
    macPart.trim();
    s = s.substring(0, colonIdx);
    s.trim();

    if (outMac != nullptr && macPart.length() > 0) {
      strncpy(outMac, macPart.c_str(), 12);
      outMac[12] = '\0';
    }
  }

  if (s == "VPRIN")    return EVT_SRC_VPRIN;
  if (s == "VBACK")    return EVT_SRC_VBACK;
  if (s == "ADC0")     return EVT_SRC_ADC0;
  if (s == "ADC1")     return EVT_SRC_ADC1;
  if (s == "DI1")      return EVT_SRC_DI1;
  if (s == "DI2")      return EVT_SRC_DI2;
  if (s == "COMM")     return EVT_SRC_COMM;
  if (s == "TEMP")     return EVT_SRC_TEMP;
  if (s == "BLE_DOOR") return EVT_SRC_BLE_DOOR;
  if (s == "BLE_MOV")  return EVT_SRC_BLE_MOV;
  if (s == "BLE_TEMP") return EVT_SRC_BLE_TEMP;
  if (s == "BLE_HUM")  return EVT_SRC_BLE_HUM;
  if (s == "BLE_BAT")  return EVT_SRC_BLE_BAT;
  if (s == "TEMP_MCU") return EVT_SRC_TEMP_MCU;
  if (s == "HEAP")     return EVT_SRC_HEAP;
  if (s == "BTN")      return EVT_SRC_BTN;

  if (s == "DT0" || s == "TIME0" || s == "DATE0") return EVT_SRC_DT0;
  if (s == "DT1" || s == "TIME1" || s == "DATE1") return EVT_SRC_DT1;
  if (s == "DT2" || s == "TIME2" || s == "DATE2") return EVT_SRC_DT2;
  if (s == "DT3" || s == "TIME3" || s == "DATE3") return EVT_SRC_DT3;

  if (s.startsWith("S")) {
    int idx = s.substring(1).toInt();
    if (idx >= 0 && idx <= 15) {
      return (EventSource)(EVT_SRC_S0 + idx);
    }
  }

  return EVT_SRC_NONE;
}

String eventOpToString(EventOperator op) {
  switch (op) {
    case EVT_OP_GT:  return ">";
    case EVT_OP_LT:  return "<";
    case EVT_OP_GTE: return ">=";
    case EVT_OP_LTE: return "<=";
    case EVT_OP_EQ:  return "==";
    case EVT_OP_NEQ: return "!=";
    default:         return "NONE";
  }
}

EventOperator stringToEventOp(const String &str) {
  String s = str;
  s.trim();
  if (s == ">")  return EVT_OP_GT;
  if (s == "<")  return EVT_OP_LT;
  if (s == ">=") return EVT_OP_GTE;
  if (s == "<=") return EVT_OP_LTE;
  if (s == "==") return EVT_OP_EQ;
  if (s == "=")  return EVT_OP_EQ;
  if (s == "!=") return EVT_OP_NEQ;
  return EVT_OP_NONE;
}

String eventActionToString(EventActionType act, uint8_t pin, uint32_t pulseSec) {
  switch (act) {
    case EVT_ACT_MQTT:
      return "MQTT";
    case EVT_ACT_OUT_ON:
      return "OUT:" + String(pin) + ":HIGH";
    case EVT_ACT_OUT_OFF:
      return "OUT:" + String(pin) + ":LOW";
    case EVT_ACT_OUT_PULSE:
      return "OUT:" + String(pin) + ":PULSE:" + String(pulseSec);
    case EVT_ACT_CYCLE_PWR:
      return "CYCLE_PWR";
    case EVT_ACT_RESTART:
      return "RESTART";
    default:
      return "MQTT";
  }
}

String eventTriggerToString(EventTriggerMode trig, uint32_t repeatSec) {
  if (trig == EVT_TRIG_REPEAT) {
    return "REPEAT:" + String(repeatSec);
  }
  return "ONESHOT";
}

static void guardarEventoNVS(int id) {
  if (id < 0 || id >= MAX_EVENTOS) return;
  prefEventos.begin("eventos", false);
  String p = "e_" + String(id) + "_";
  prefEventos.putBool((p + "en").c_str(),  eventRules[id].enabled);
  prefEventos.putInt((p + "src").c_str(),  (int)eventRules[id].source);
  prefEventos.putString((p + "mac").c_str(), eventRules[id].bleMac);
  prefEventos.putInt((p + "op").c_str(),   (int)eventRules[id].op);
  prefEventos.putFloat((p + "th").c_str(), eventRules[id].threshold);
  prefEventos.putUInt((p + "ton").c_str(), eventRules[id].tonSeconds);
  prefEventos.putInt((p + "act").c_str(),  (int)eventRules[id].actionType);
  prefEventos.putUChar((p + "pin").c_str(),eventRules[id].outPin);
  prefEventos.putUInt((p + "pls").c_str(), eventRules[id].pulseSeconds);
  prefEventos.putInt((p + "trg").c_str(),  (int)eventRules[id].triggerMode);
  prefEventos.putUInt((p + "rep").c_str(), eventRules[id].repeatSeconds);
  prefEventos.end();
}

static void cargarEventosNVS() {
  prefEventos.begin("eventos", true);
  for (int i = 0; i < MAX_EVENTOS; i++) {
    String p = "e_" + String(i) + "_";
    eventRules[i].enabled      = prefEventos.getBool((p + "en").c_str(), false);
    eventRules[i].source       = (EventSource)prefEventos.getInt((p + "src").c_str(), (int)EVT_SRC_NONE);
    String mac = prefEventos.getString((p + "mac").c_str(), "");
    memset(eventRules[i].bleMac, 0, sizeof(eventRules[i].bleMac));
    if (mac.length() > 0) {
      strncpy(eventRules[i].bleMac, mac.c_str(), 12);
      eventRules[i].bleMac[12] = '\0';
    }
    eventRules[i].op           = (EventOperator)prefEventos.getInt((p + "op").c_str(), (int)EVT_OP_NONE);
    eventRules[i].threshold    = prefEventos.getFloat((p + "th").c_str(), 0.0f);
    eventRules[i].tonSeconds   = prefEventos.getUInt((p + "ton").c_str(), 0);
    eventRules[i].actionType   = (EventActionType)prefEventos.getInt((p + "act").c_str(), (int)EVT_ACT_MQTT);
    eventRules[i].outPin       = prefEventos.getUChar((p + "pin").c_str(), 0);
    eventRules[i].pulseSeconds = prefEventos.getUInt((p + "pls").c_str(), 0);
    eventRules[i].triggerMode  = (EventTriggerMode)prefEventos.getInt((p + "trg").c_str(), (int)EVT_TRIG_ONESHOT);
    eventRules[i].repeatSeconds= prefEventos.getUInt((p + "rep").c_str(), 60);

    // Reiniciar variables dinámicas de ejecución
    eventRules[i].conditionStartTime = 0;
    eventRules[i].conditionMet       = false;
    eventRules[i].eventFired         = false;
    eventRules[i].lastFiredTime      = 0;
    eventRules[i].pulseStartTime     = 0;
    eventRules[i].pulseActive        = false;
  }
  prefEventos.end();
}

static void guardarDisparadorFechaHoraNVS(int id) {
  if (id < 0 || id >= MAX_DT_TRIGGERS) return;
  prefEventos.begin("eventos", false);
  String p = "dt_" + String(id) + "_";
  prefEventos.putBool((p + "en").c_str(),   dtTriggers[id].enabled);
  prefEventos.putShort((p + "yr").c_str(),  dtTriggers[id].year);
  prefEventos.putChar((p + "mo").c_str(),   dtTriggers[id].month);
  prefEventos.putChar((p + "dy").c_str(),   dtTriggers[id].day);
  prefEventos.putChar((p + "hr").c_str(),   dtTriggers[id].hour);
  prefEventos.putChar((p + "mi").c_str(),   dtTriggers[id].minute);
  prefEventos.putChar((p + "sc").c_str(),   dtTriggers[id].second);
  prefEventos.end();
}

static void cargarDisparadoresFechaHoraNVS() {
  prefEventos.begin("eventos", true);
  for (int i = 0; i < MAX_DT_TRIGGERS; i++) {
    String p = "dt_" + String(i) + "_";
    dtTriggers[i].enabled = prefEventos.getBool((p + "en").c_str(), false);
    dtTriggers[i].year    = prefEventos.getShort((p + "yr").c_str(), -1);
    dtTriggers[i].month   = prefEventos.getChar((p + "mo").c_str(), -1);
    dtTriggers[i].day     = prefEventos.getChar((p + "dy").c_str(), -1);
    dtTriggers[i].hour    = prefEventos.getChar((p + "hr").c_str(), 0);
    dtTriggers[i].minute  = prefEventos.getChar((p + "mi").c_str(), 0);
    dtTriggers[i].second  = prefEventos.getChar((p + "sc").c_str(), 0);
  }
  prefEventos.end();
}

static float evaluarDisparadorFechaHora(int id) {
  if (id < 0 || id >= MAX_DT_TRIGGERS) return 0.0f;
  const DateTimeTrigger &dt = dtTriggers[id];
  if (!dt.enabled) return 0.0f;

  time_t now;
  time(&now);
  if (now < 100000) {
    // Reloj RTC aún no sincronizado
    return 0.0f;
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  int curYear  = timeinfo.tm_year + 1900;
  int curMonth = timeinfo.tm_mon + 1;
  int curDay   = timeinfo.tm_mday;
  int curHour  = timeinfo.tm_hour;
  int curMin   = timeinfo.tm_min;
  int curSec   = timeinfo.tm_sec;

  if (dt.year   >= 0 && dt.year   != curYear)  return 0.0f;
  if (dt.month  >= 0 && dt.month  != curMonth) return 0.0f;
  if (dt.day    >= 0 && dt.day    != curDay)   return 0.0f;
  if (dt.hour   >= 0 && dt.hour   != curHour)  return 0.0f;
  if (dt.minute >= 0 && dt.minute != curMin)   return 0.0f;
  if (dt.second >= 0) {
    // Ventana de 5 segundos desde el segundo indicado para evitar pérdidas por latencia de red/BLE
    if (curSec < dt.second || curSec >= (dt.second + 5)) return 0.0f;
  }

  return 1.0f;
}

static bool parsearCampoFechaHora(String token, int minVal, int maxVal, int16_t &outVal) {
  token.trim();
  if (token == "*" || token.equalsIgnoreCase("X") || token.equalsIgnoreCase("XX") || token.equalsIgnoreCase("XXXX")) {
    outVal = -1;
    return true;
  }
  if (token.length() == 0) return false;
  for (unsigned int i = 0; i < token.length(); i++) {
    if (!isDigit(token[i])) return false;
  }
  int val = token.toInt();
  if (val < minVal || val > maxVal) return false;
  outVal = (int16_t)val;
  return true;
}

bool configurarDisparadorFechaHora(int id, const String &paramStr, String &err) {
  if (id < 0 || id >= MAX_DT_TRIGGERS) {
    err = "Indice DT fuera de rango (0-" + String(MAX_DT_TRIGGERS - 1) + ")";
    return false;
  }

  String s = paramStr;
  s.trim();

  if (s == "0" || s.equalsIgnoreCase("OFF") || s.equalsIgnoreCase("DISABLE")) {
    dtTriggers[id].enabled = false;
    guardarDisparadorFechaHoraNVS(id);
    return true;
  }

  // Permitir prefijo opcional "1," (ej: DVL+SDT=0,1,2026-11-20 14:30:00)
  if (s.startsWith("1,")) {
    s = s.substring(2);
    s.trim();
  }

  String datePart = "";
  String timePart = "";

  int spaceIdx = s.indexOf(' ');
  if (spaceIdx != -1) {
    datePart = s.substring(0, spaceIdx);
    timePart = s.substring(spaceIdx + 1);
    datePart.trim();
    timePart.trim();
  } else {
    // Si solo contiene ':' asumimos horario diario recurrente (*-*-* HH:MM[:SS])
    if (s.indexOf(':') != -1 && s.indexOf('-') == -1) {
      datePart = "*-*-*";
      timePart = s;
    } else {
      err = "Formato invalido. Use YYYY-MM-DD HH:MM:SS, *-*-* HH:MM:SS o HH:MM:SS (o 0 para desactivar)";
      return false;
    }
  }

  // Parsear fecha: YYYY-MM-DD
  int d1 = datePart.indexOf('-');
  int d2 = datePart.indexOf('-', d1 + 1);
  if (d1 == -1 || d2 == -1) {
    err = "Formato de fecha invalido (" + datePart + "). Use YYYY-MM-DD o *-*-*";
    return false;
  }

  int16_t yr = -1, mo = -1, dy = -1;
  if (!parsearCampoFechaHora(datePart.substring(0, d1), 2020, 2099, yr)) {
    err = "Anio invalido (rango 2020-2099 o *)";
    return false;
  }
  if (!parsearCampoFechaHora(datePart.substring(d1 + 1, d2), 1, 12, mo)) {
    err = "Mes invalido (rango 1-12 o *)";
    return false;
  }
  if (!parsearCampoFechaHora(datePart.substring(d2 + 1), 1, 31, dy)) {
    err = "Dia invalido (rango 1-31 o *)";
    return false;
  }

  // Parsear hora: HH:MM o HH:MM:SS
  int t1 = timePart.indexOf(':');
  if (t1 == -1) {
    err = "Formato de hora invalido (" + timePart + "). Use HH:MM:SS o HH:MM";
    return false;
  }
  int t2 = timePart.indexOf(':', t1 + 1);

  int16_t hr = -1, mi = -1, sc = -1;
  String hStr = timePart.substring(0, t1);
  String mStr = (t2 != -1) ? timePart.substring(t1 + 1, t2) : timePart.substring(t1 + 1);
  String sStr = (t2 != -1) ? timePart.substring(t2 + 1) : "*";

  if (!parsearCampoFechaHora(hStr, 0, 23, hr)) {
    err = "Hora invalida (rango 0-23 o *)";
    return false;
  }
  if (!parsearCampoFechaHora(mStr, 0, 59, mi)) {
    err = "Minuto invalido (rango 0-59 o *)";
    return false;
  }
  if (!parsearCampoFechaHora(sStr, 0, 59, sc)) {
    err = "Segundo invalido (rango 0-59 o *)";
    return false;
  }

  dtTriggers[id].enabled = true;
  dtTriggers[id].year    = yr;
  dtTriggers[id].month   = (int8_t)mo;
  dtTriggers[id].day     = (int8_t)dy;
  dtTriggers[id].hour    = (int8_t)hr;
  dtTriggers[id].minute  = (int8_t)mi;
  dtTriggers[id].second  = (int8_t)sc;

  guardarDisparadorFechaHoraNVS(id);
  return true;
}

static String formatearCampo2Digitos(int val) {
  if (val < 0) return "*";
  if (val < 10) return "0" + String(val);
  return String(val);
}

String obtenerDisparadorFechaHora(int id) {
  if (id < 0 || id >= MAX_DT_TRIGGERS) {
    return "[ERR] Indice DT invalido (0-3)";
  }
  const DateTimeTrigger &dt = dtTriggers[id];
  if (!dt.enabled) {
    return "DT" + String(id) + "=DESHABILITADO";
  }

  String yrStr = (dt.year < 0) ? "*" : String(dt.year);
  String fechaHora = yrStr + "-" +
                     formatearCampo2Digitos(dt.month) + "-" +
                     formatearCampo2Digitos(dt.day) + " " +
                     formatearCampo2Digitos(dt.hour) + ":" +
                     formatearCampo2Digitos(dt.minute) + ":" +
                     formatearCampo2Digitos(dt.second);

  int estadoActual = (evaluarDisparadorFechaHora(id) > 0.5f) ? 1 : 0;
  return "DT" + String(id) + "=" + fechaHora + " (ESTADO=" + String(estadoActual) + ")";
}

String obtenerTodosDisparadoresFechaHora() {
  String rtcNow = printCurrentTime();
  if (rtcNow.length() == 0) rtcNow = "SIN SINCRONIZAR";
  String out = "RTC=" + rtcNow + "\n";
  for (int i = 0; i < MAX_DT_TRIGGERS; i++) {
    out += obtenerDisparadorFechaHora(i);
    if (i < MAX_DT_TRIGGERS - 1) out += "\n";
  }
  return out;
}

void initMotorEventos() {
  memset(bleTable, 0, sizeof(bleTable));
  bleDeviceCount = 0;
  lastUpdatedBleIndex = -1;

  cargarEventosNVS();
  cargarDisparadoresFechaHoraNVS();

  // Configurar pines de hardware según reglas habilitadas
  for (int i = 0; i < MAX_EVENTOS; i++) {
    if (!eventRules[i].enabled) continue;

    // Configurar entradas digitales si están en uso
    if (eventRules[i].source == EVT_SRC_DI1) {
      pinMode(PIN_EVT_DI1, INPUT_PULLUP);
    } else if (eventRules[i].source == EVT_SRC_DI2) {
      pinMode(PIN_EVT_DI2, INPUT_PULLUP);
    } else if (eventRules[i].source == EVT_SRC_BTN) {
      pinMode(PIN_EVT_BTN, INPUT_PULLUP);
    }

    // Configurar pin de salida si aplica
    if (eventRules[i].outPin > 0) {
      pinMode(eventRules[i].outPin, OUTPUT);
      digitalWrite(eventRules[i].outPin, LOW);
    }
  }

  DVL_PRINTLN("[EVENTOS] Motor de Eventos (Micro-PLC) inicializado.");
}

static float obtenerValorFuente(EventSource src, float vPrin, float vBack, float adc0, float adc1, const char *bleMac = nullptr) {
  // Canales S0..S15 de la placa expansora serial
  if (src >= EVT_SRC_S0 && src <= EVT_SRC_S15) {
    int idx = src - EVT_SRC_S0;
    if (sensorValues[idx].length() > 0 && sensorValues[idx] != "nan") {
      return sensorValues[idx].toFloat();
    }
    return 0.0f;
  }

  // Disparadores programables por Fecha y Hora (DT0..DT3)
  if (src >= EVT_SRC_DT0 && src <= EVT_SRC_DT3) {
    return evaluarDisparadorFechaHora(src - EVT_SRC_DT0);
  }

  switch (src) {
    case EVT_SRC_VPRIN:    return vPrin;
    case EVT_SRC_VBACK:    return vBack;
    case EVT_SRC_ADC0:     return adc0;
    case EVT_SRC_ADC1:     return adc1;
    case EVT_SRC_DI1:      return (float)digitalRead(PIN_EVT_DI1);
    case EVT_SRC_DI2:      return (float)digitalRead(PIN_EVT_DI2);
    case EVT_SRC_COMM:     return mqtt.connected() ? 1.0f : 0.0f;
    case EVT_SRC_TEMP: {
      if (valorStr.length() > 0 && valorStr != "nan") {
        return valorStr.toFloat();
      }
      return 0.0f;
    }
    case EVT_SRC_BLE_DOOR:
    case EVT_SRC_BLE_MOV:
    case EVT_SRC_BLE_TEMP:
    case EVT_SRC_BLE_HUM:
    case EVT_SRC_BLE_BAT: {
      int idx = buscarDispositivoBLE(bleMac);
      if (idx == -1) return 0.0f;
      switch (src) {
        case EVT_SRC_BLE_DOOR: return bleTable[idx].door;
        case EVT_SRC_BLE_MOV:  return bleTable[idx].mov;
        case EVT_SRC_BLE_TEMP: return bleTable[idx].temp;
        case EVT_SRC_BLE_HUM:  return bleTable[idx].hum;
        case EVT_SRC_BLE_BAT:  return bleTable[idx].bat;
        default: return 0.0f;
      }
    }
    case EVT_SRC_TEMP_MCU: return temperatureRead();
    case EVT_SRC_HEAP:     return (float)ESP.getFreeHeap();
    case EVT_SRC_BTN:      return (float)digitalRead(PIN_EVT_BTN);
    default:               return 0.0f;
  }
}

static bool evaluarCondicion(float valor, EventOperator op, float umbral) {
  switch (op) {
    case EVT_OP_GT:  return (valor > umbral);
    case EVT_OP_LT:  return (valor < umbral);
    case EVT_OP_GTE: return (valor >= umbral);
    case EVT_OP_LTE: return (valor <= umbral);
    case EVT_OP_EQ:  return (abs(valor - umbral) < 0.001f);
    case EVT_OP_NEQ: return (abs(valor - umbral) >= 0.001f);
    default:         return false;
  }
}

static void emitirReporteEventoMQTT(int id, float valActual) {
  StaticJsonDocument<384> doc;
  doc["ident"]     = ident;
  doc["status"]    = "event-triggered";
  doc["event_id"]  = id;
  doc["date"]      = printCurrentTime();
  doc["src"]       = eventSourceToString(eventRules[id].source, eventRules[id].bleMac);
  if (strlen(eventRules[id].bleMac) > 0) {
    doc["ble_mac"] = eventRules[id].bleMac;
  }
  doc["val"]       = valActual;
  doc["op"]        = eventOpToString(eventRules[id].op);
  doc["threshold"] = eventRules[id].threshold;
  doc["ton"]       = eventRules[id].tonSeconds;
  doc["action"]    = eventActionToString(eventRules[id].actionType, eventRules[id].outPin, eventRules[id].pulseSeconds);

  char payload[384];
  serializeJson(doc, payload, sizeof(payload));

  String topicEvt = topic1 + "/EVENTOS";
  DVL_PRINTF("[EVENTOS] Disparo Regla EV%d -> Publicando en %s: %s\n", id, topicEvt.c_str(), payload);

  if (mqtt.connected()) {
    publish_mqtt_json(topicEvt, String(payload));
  } else {
    flash_save_packet(payload);
    DVL_PRINTLN("[EVENTOS] MQTT desconectado. Evento guardado en Flash SPIFFS.");
  }
}

static void ejecutarAccionEvento(int id, float valActual) {
  EventRule &r = eventRules[id];

  // Acción física sobre pines
  if (r.actionType == EVT_ACT_OUT_ON && r.outPin > 0) {
    pinMode(r.outPin, OUTPUT);
    digitalWrite(r.outPin, HIGH);
    DVL_PRINTF("[EVENTOS] EV%d: Salida GPIO%d activada (HIGH).\n", id, r.outPin);
  } else if (r.actionType == EVT_ACT_OUT_OFF && r.outPin > 0) {
    pinMode(r.outPin, OUTPUT);
    digitalWrite(r.outPin, LOW);
    r.pulseActive = false;
    DVL_PRINTF("[EVENTOS] EV%d: Salida GPIO%d desactivada (LOW).\n", id, r.outPin);
  } else if (r.actionType == EVT_ACT_OUT_PULSE && r.outPin > 0) {
    pinMode(r.outPin, OUTPUT);
    digitalWrite(r.outPin, HIGH);
    r.pulseActive = true;
    r.pulseStartTime = millis();
    DVL_PRINTF("[EVENTOS] EV%d: Salida GPIO%d pulso iniciado por %d s.\n", id, r.outPin, r.pulseSeconds);
  } else if (r.actionType == EVT_ACT_CYCLE_PWR) {
    DVL_PRINTF("[EVENTOS] EV%d: Ciclando alimentacion (SENSOR_POWER_PIN GPIO%d)...\n", id, SENSOR_POWER_PIN);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    delay(1000);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    DVL_PRINTLN("[EVENTOS] Alimentacion reestablecida.");
  }

  // Notificación obligatoria por MQTT / Flash
  emitirReporteEventoMQTT(id, valActual);

  if (r.actionType == EVT_ACT_RESTART) {
    DVL_PRINTLN("[EVENTOS] Accion RESTART solicitada. Reiniciando microcontrolador en 1 segundo...");
    delay(1000);
    ESP.restart();
  }
}

void procesarMotorEventos(float vPrin, float vBack, float adc0, float adc1) {
  unsigned long now = millis();

  for (int i = 0; i < MAX_EVENTOS; i++) {
    EventRule &r = eventRules[i];

    // Manejo del temporizador de pulso en salida física
    if (r.pulseActive && r.outPin > 0) {
      if (now - r.pulseStartTime >= (r.pulseSeconds * 1000UL)) {
        digitalWrite(r.outPin, LOW);
        r.pulseActive = false;
        DVL_PRINTF("[EVENTOS] EV%d: Pulso de salida en GPIO%d completado (LOW).\n", i, r.outPin);
      }
    }

    if (!r.enabled || r.source == EVT_SRC_NONE || r.op == EVT_OP_NONE) {
      continue;
    }

    float valActual = obtenerValorFuente(r.source, vPrin, vBack, adc0, adc1, r.bleMac);
    bool cumple = evaluarCondicion(valActual, r.op, r.threshold);

    if (cumple) {
      // Condición verdadera
      if (!r.conditionMet) {
        // Flanco de inicio de condición
        r.conditionMet = true;
        r.conditionStartTime = now;
      }

      unsigned long duracion = (now - r.conditionStartTime) / 1000UL;
      if (duracion >= r.tonSeconds) {
        // Temporizador TON cumplido
        bool debeDisparar = false;

        if (r.triggerMode == EVT_TRIG_ONESHOT) {
          if (!r.eventFired) {
            debeDisparar = true;
          }
        } else if (r.triggerMode == EVT_TRIG_REPEAT) {
          if (!r.eventFired || (now - r.lastFiredTime >= (r.repeatSeconds * 1000UL))) {
            debeDisparar = true;
          }
        }

        if (debeDisparar) {
          ejecutarAccionEvento(i, valActual);
          r.eventFired = true;
          r.lastFiredTime = now;
        }
      }
    } else {
      // Condición falsa: volver a reposo y resetear TON
      r.conditionMet = false;
      r.conditionStartTime = 0;
      r.eventFired = false; // Rearmar ONESHOT para cuando vuelva a ocurrir
    }
  }
}

bool configurarEvento(int id, const String &paramStr, String &err) {
  if (id < 0 || id >= MAX_EVENTOS) {
    err = "ID fuera de rango (0-" + String(MAX_EVENTOS - 1) + ")";
    return false;
  }

  // Si se envía solo el estado de deshabilitación: DVL+SEVT=<id>,0
  String s = paramStr;
  s.trim();
  if (s == "0") {
    eventRules[id].enabled = false;
    guardarEventoNVS(id);
    return true;
  }

  // Formato completo: <en>,<fuente>,<operador>,<umbral>,<ton_seg>,<accion>,<rearme>
  // Ejemplo: 1,VPRIN,<,11.5,30,MQTT,ONESHOT
  // Ejemplo: 1,TEMP,>,50.0,15,OUT:15:HIGH,ONESHOT
  // Ejemplo: 1,BLE_DOOR:F074BFF03C91,==,1,3,OUT:15:PULSE:60,ONESHOT
  // Ejemplo: 1,BLE_TEMP:D47DA699A56B,>,28.0,10,MQTT,ONESHOT
  // Ejemplo: 1,TEMP_MCU,>,75.0,60,MQTT,ONESHOT
  int count = 0;
  int lastIdx = 0;

  String parts[8];
  while (count < 7) {
    int comma = s.indexOf(',', lastIdx);
    if (comma == -1) {
      parts[count++] = s.substring(lastIdx);
      break;
    }
    parts[count++] = s.substring(lastIdx, comma);
    lastIdx = comma + 1;
  }

  if (count < 7) {
    err = "Cantidad insuficiente de parametros (se esperan 7: <en>,<src>,<op>,<th>,<ton>,<act>,<trg>)";
    return false;
  }

  for (int i = 0; i < 7; i++) {
    parts[i].trim();
  }

  bool en = (parts[0] == "1");
  char parsedMac[13] = {0};
  EventSource src = stringToEventSource(parts[1], parsedMac);
  if (src == EVT_SRC_NONE) {
    err = "Fuente invalida (" + parts[1] + "). Use: VPRIN, VBACK, ADC0, ADC1, DI1, DI2, COMM, TEMP, S0..S15, BLE_DOOR[:MAC], BLE_MOV[:MAC], BLE_TEMP[:MAC], BLE_HUM[:MAC], BLE_BAT[:MAC], TEMP_MCU, HEAP, BTN, DT0..DT3";
    return false;
  }

  EventOperator op = stringToEventOp(parts[2]);
  if (op == EVT_OP_NONE) {
    err = "Operador invalido (" + parts[2] + "). Use: >, <, >=, <=, ==, !=";
    return false;
  }

  float th = parts[3].toFloat();
  uint32_t ton = (uint32_t)strtoul(parts[4].c_str(), NULL, 10);

  // Parsear acción
  EventActionType act = EVT_ACT_MQTT;
  uint8_t outPin = 0;
  uint32_t pulseSec = 0;
  String actStr = parts[5];
  actStr.toUpperCase();

  if (actStr == "MQTT") {
    act = EVT_ACT_MQTT;
  } else if (actStr == "CYCLE_PWR") {
    act = EVT_ACT_CYCLE_PWR;
  } else if (actStr == "RESTART") {
    act = EVT_ACT_RESTART;
  } else if (actStr.startsWith("OUT:")) {
    // OUT:<pin>:<HIGH|LOW|PULSE:seg>
    int colon1 = actStr.indexOf(':', 4);
    if (colon1 == -1) {
      err = "Accion OUT invalida. Use OUT:<pin>:HIGH, OUT:<pin>:LOW o OUT:<pin>:PULSE:<seg>";
      return false;
    }
    outPin = (uint8_t)actStr.substring(4, colon1).toInt();
    String subMode = actStr.substring(colon1 + 1);

    if (subMode == "HIGH" || subMode == "ON" || subMode == "1") {
      act = EVT_ACT_OUT_ON;
    } else if (subMode == "LOW" || subMode == "OFF" || subMode == "0") {
      act = EVT_ACT_OUT_OFF;
    } else if (subMode.startsWith("PULSE:")) {
      act = EVT_ACT_OUT_PULSE;
      pulseSec = (uint32_t)subMode.substring(6).toInt();
      if (pulseSec == 0) pulseSec = 1;
    } else {
      err = "Sub-modo OUT desconocido (" + subMode + ")";
      return false;
    }
  } else {
    err = "Accion desconocida (" + parts[5] + ")";
    return false;
  }

  // Parsear modo de disparo / rearme
  EventTriggerMode trg = EVT_TRIG_ONESHOT;
  uint32_t repSec = 60;
  String trgStr = parts[6];
  trgStr.toUpperCase();

  if (trgStr == "ONESHOT" || trgStr == "ONE") {
    trg = EVT_TRIG_ONESHOT;
  } else if (trgStr.startsWith("REPEAT")) {
    trg = EVT_TRIG_REPEAT;
    int col = trgStr.indexOf(':');
    if (col != -1) {
      repSec = (uint32_t)trgStr.substring(col + 1).toInt();
    }
    if (repSec == 0) repSec = 60;
  } else {
    err = "Modo disparo invalido (" + parts[6] + "). Use ONESHOT o REPEAT:<seg>";
    return false;
  }

  // Asignar y persistir
  eventRules[id].enabled      = en;
  eventRules[id].source       = src;
  memset(eventRules[id].bleMac, 0, sizeof(eventRules[id].bleMac));
  if (parsedMac[0] != '\0') {
    strncpy(eventRules[id].bleMac, parsedMac, sizeof(eventRules[id].bleMac) - 1);
  }
  eventRules[id].op           = op;
  eventRules[id].threshold    = th;
  eventRules[id].tonSeconds   = ton;
  eventRules[id].actionType   = act;
  eventRules[id].outPin       = outPin;
  eventRules[id].pulseSeconds = pulseSec;
  eventRules[id].triggerMode  = trg;
  eventRules[id].repeatSeconds= repSec;

  // Resetear estados dinámicos
  eventRules[id].conditionStartTime = 0;
  eventRules[id].conditionMet       = false;
  eventRules[id].eventFired         = false;
  eventRules[id].pulseActive        = false;

  if (outPin > 0) {
    pinMode(outPin, OUTPUT);
    digitalWrite(outPin, LOW);
  }
  if (src == EVT_SRC_DI1) {
    pinMode(PIN_EVT_DI1, INPUT_PULLUP);
  } else if (src == EVT_SRC_DI2) {
    pinMode(PIN_EVT_DI2, INPUT_PULLUP);
  } else if (src == EVT_SRC_BTN) {
    pinMode(PIN_EVT_BTN, INPUT_PULLUP);
  }

  guardarEventoNVS(id);
  return true;
}

String obtenerConfiguracionEvento(int id) {
  if (id < 0 || id >= MAX_EVENTOS) {
    return "[ERR] ID invalido (0-3)";
  }
  EventRule &r = eventRules[id];
  if (!r.enabled) {
    return "EV" + String(id) + "=DESHABILITADO";
  }

  return "EV" + String(id) + "=" +
         (r.enabled ? "1," : "0,") +
         eventSourceToString(r.source, r.bleMac) + "," +
         eventOpToString(r.op) + "," +
         String(r.threshold, 2) + "," +
         String(r.tonSeconds) + "," +
         eventActionToString(r.actionType, r.outPin, r.pulseSeconds) + "," +
         eventTriggerToString(r.triggerMode, r.repeatSeconds);
}

String obtenerEstadoEventos(float vPrin, float vBack, float adc0, float adc1) {
  unsigned long now = millis();
  String out = "========== ESTADO MOTOR DE EVENTOS (PLC) ==========\n";
  for (int i = 0; i < MAX_EVENTOS; i++) {
    EventRule &r = eventRules[i];
    out += "EV" + String(i) + ": ";
    if (!r.enabled) {
      out += "DESHABILITADO\n";
      continue;
    }

    float valActual = obtenerValorFuente(r.source, vPrin, vBack, adc0, adc1, r.bleMac);
    bool cumple = evaluarCondicion(valActual, r.op, r.threshold);
    unsigned long segsTON = 0;
    if (r.conditionMet) {
      segsTON = (now - r.conditionStartTime) / 1000UL;
    }

    out += (r.enabled ? "HABILITADO | " : "OFF | ");
    out += eventSourceToString(r.source, r.bleMac) + " (act=" + String(valActual, 2) + ") " +
           eventOpToString(r.op) + " " + String(r.threshold, 2) + " -> ";
    out += (cumple ? "CUMPLE" : "NO CUMPLE");
    out += " | TON: " + String(segsTON) + "/" + String(r.tonSeconds) + "s";
    out += " | Disparado: " + String(r.eventFired ? "SI" : "NO");
    if (r.outPin > 0) {
      out += " | Out GPIO" + String(r.outPin) + "=" + String(digitalRead(r.outPin));
      if (r.pulseActive) out += " (PULSO ACTIVO)";
    }
    out += "\n";
  }
  out += "---------- DISPARADORES FECHA/HORA (DT) -----------\n";
  out += obtenerTodosDisparadoresFechaHora() + "\n";
  out += "===================================================";
  return out;
}
