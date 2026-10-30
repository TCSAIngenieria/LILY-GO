#include "GPRS.h"
#include "Debug.h"
#include <PubSubClient.h>
#include <time.h>
#include <sys/time.h>

#define GSM_PIN ""
#define SerialAT Serial1

extern String apn;
extern PubSubClient mqtt;

// Configuraciones de diagnostico y modo de tecnologia
uint8_t modemDebugLevel = 0; // 0=Normal, 1=Detallado, 2=Detallado + Raw AT
uint8_t modemTechMode = 0;   // 0=Auto, 1=Fijo CAT-M1, 2=Fijo NB-IoT, 3=Fijo 2G

ModemDebugStream modemStream(SerialAT);

// Estados internos para el manejo de red
bool isModemReady = false;
bool isModemConnected = false;
bool isGprsActive = false;
static bool modeConfigured = false;
static unsigned long lastConnectAttempt = 0;
static unsigned long modeWindowStart = 0;
static unsigned long lastCheckConnected = 0;
static int initFailCount = 0;
static int failedFullCycles = 0;

const unsigned long checkInterval = 3000;       // Sondeo de registro cada 3 segundos
const unsigned long modeWindowTimeout = 45000;  // 45 segundos de espera por tecnologia antes de rotar

// Indice de tecnologia actual:
// 0 -> LTE CAT-M1 (CNMP=38, CMNB=1)
// 1 -> LTE NB-IoT (CNMP=38, CMNB=2)
// 2 -> 2G GSM/GPRS (CNMP=13)
int currentModeIndex = 0;
static String activeTechDetected = "";

static time_t internalTime = 0;

// ==================== Implementacion de ModemDebugStream ====================

int ModemDebugStream::available() {
  return _stream.available();
}

int ModemDebugStream::read() {
  int c = _stream.read();
  if (c >= 0 && modemDebugLevel >= 2) {
    if (c == '\n') {
      _rxLine.trim();
      if (_rxLine.length() > 0) {
        DVL_PRINTF("[AT << %lu ms] %s\n", millis(), _rxLine.c_str());
      }
      _rxLine = "";
    } else if (c != '\r') {
      if (_rxLine.length() < 240) {
        _rxLine += (char)c;
      }
    }
  }
  return c;
}

int ModemDebugStream::peek() {
  return _stream.peek();
}

void ModemDebugStream::flush() {
  _stream.flush();
}

size_t ModemDebugStream::write(uint8_t c) {
  if (modemDebugLevel >= 2) {
    if (c == '\n') {
      _txLine.trim();
      if (_txLine.length() > 0) {
        DVL_PRINTF("[AT >> %lu ms] %s\n", millis(), _txLine.c_str());
      }
      _txLine = "";
    } else if (c != '\r') {
      if (_txLine.length() < 240) {
        _txLine += (char)c;
      }
    }
  }
  return _stream.write(c);
}

size_t ModemDebugStream::write(const uint8_t *buffer, size_t size) {
  size_t n = 0;
  for (size_t i = 0; i < size; i++) {
    n += write(buffer[i]);
  }
  return n;
}

// ==================== Helpers de Diagnostico ====================

static String decodeRegStat(int stat) {
  switch (stat) {
    case 0: return "0 (No registrado, sin buscar)";
    case 1: return "1 (Registrado - Red Local/Home)";
    case 2: return "2 (No registrado, buscando operador...)";
    case 3: return "3 (Registro DENEGADO por la red)";
    case 4: return "4 (Desconocido)";
    case 5: return "5 (Registrado - Roaming)";
    default: return String(stat) + " (Sin respuesta)";
  }
}

static int queryRegStatusCode(TinyGsm &modem, const char *cmd, const char *prefix) {
  String res;
  modem.sendAT(cmd);
  if (modem.waitResponse(1000L, res) == 1) {
    int idx = res.indexOf(prefix);
    if (idx != -1) {
      int comma = res.indexOf(',', idx);
      if (comma != -1) {
        int end = res.indexOf('\r', comma + 1);
        String val = (end != -1) ? res.substring(comma + 1, end) : res.substring(comma + 1);
        int secondComma = val.indexOf(',');
        if (secondComma != -1) {
          val = val.substring(0, secondComma);
        }
        val.trim();
        return val.toInt();
      }
    }
  }
  return -1;
}

static String queryCPSI_Line(TinyGsm &modem) {
  String res;
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    int idx = res.indexOf("+CPSI:");
    if (idx != -1) {
      int end = res.indexOf('\r', idx);
      String line = (end != -1) ? res.substring(idx + 6, end) : res.substring(idx + 6);
      line.trim();
      return line;
    }
  }
  return "N/A";
}

static void refreshDetectedTechFromCPSI(const String &cpsiLine) {
  if (cpsiLine.indexOf("CAT-M1") != -1 || cpsiLine.indexOf("LTE M1") != -1) {
    activeTechDetected = "LTE CAT-M1 (eMTC)";
  } else if (cpsiLine.indexOf("NB-IoT") != -1 || cpsiLine.indexOf("NB-IOT") != -1) {
    activeTechDetected = "NB-IoT";
  } else if (cpsiLine.indexOf("GSM") != -1) {
    activeTechDetected = "2G (GSM/GPRS/EDGE)";
  }
}

String getModemTechModeName(uint8_t mode) {
  switch (mode) {
    case 0: return "AUTO (CAT-M1 -> NB-IoT -> 2G)";
    case 1: return "FIJO LTE CAT-M1 (CNMP=38, CMNB=1)";
    case 2: return "FIJO LTE NB-IoT (CNMP=38, CMNB=2)";
    case 3: return "FIJO 2G GSM/GPRS (CNMP=13)";
    default: return "DESCONOCIDO";
  }
}

void forceModemTechReconfig() {
  modeConfigured = false;
  isModemConnected = false;
  isGprsActive = false;
  activeTechDetected = "";
  lastConnectAttempt = 0;
  modeWindowStart = 0;
  failedFullCycles = 0;
  mqtt.disconnect();
  if (modemTechMode == 1) currentModeIndex = 0;
  else if (modemTechMode == 2) currentModeIndex = 1;
  else if (modemTechMode == 3) currentModeIndex = 2;
  else currentModeIndex = 0;
}

void notifyModemReset() {
  isModemReady = false;
  isModemConnected = false;
  isGprsActive = false;
  modeConfigured = false;
  activeTechDetected = "";
  lastConnectAttempt = 0;
  modeWindowStart = 0;
  mqtt.disconnect();
}

void preconfigureModemAPN(TinyGsm &modem, const String &apnStr) {
  if (apnStr.length() == 0) return;
  if (modemDebugLevel >= 1) {
    DVL_PRINTLN("[MODEM-DBG] Pre-configurando contexto PDP con APN: " + apnStr);
  }
  modem.sendAT("+CGDCONT=1,\"IP\",\"", apnStr, "\"");
  modem.waitResponse(2000L);
}

static bool applyModemRadioTech(TinyGsm &modem, int idx) {
  bool okCnmp = false;
  bool okCmnb = true;

  if (idx == 0) {
    // LTE CAT-M1 (eMTC): CNMP=38 (Solo LTE) + CMNB=1 (CAT-M1)
    if (modemDebugLevel >= 1) {
      DVL_PRINTLN("[MODEM-DBG] Aplicando modo radio: AT+CNMP=38 (LTE) + AT+CMNB=1 (CAT-M1)...");
    }
    okCnmp = modem.setNetworkMode(38);
    okCmnb = modem.setPreferredMode(1);
  } else if (idx == 1) {
    // LTE NB-IoT: CNMP=38 (Solo LTE) + CMNB=2 (NB-IoT)
    if (modemDebugLevel >= 1) {
      DVL_PRINTLN("[MODEM-DBG] Aplicando modo radio: AT+CNMP=38 (LTE) + AT+CMNB=2 (NB-IoT)...");
    }
    okCnmp = modem.setNetworkMode(38);
    okCmnb = modem.setPreferredMode(2);
  } else {
    // 2G GSM/GPRS: CNMP=13 (Solo GSM)
    if (modemDebugLevel >= 1) {
      DVL_PRINTLN("[MODEM-DBG] Aplicando modo radio: AT+CNMP=13 (Solo 2G GSM/GPRS)...");
    }
    okCnmp = modem.setNetworkMode(13);
  }

  if (modemDebugLevel >= 1) {
    DVL_PRINT("[MODEM-DBG] Resultado configuracion radio -> CNMP: ");
    DVL_PRINT(okCnmp ? "OK" : "ERROR");
    if (idx == 0 || idx == 1) {
      DVL_PRINT(" | CMNB: ");
      DVL_PRINT(okCmnb ? "OK" : "ERROR");
    }
    DVL_PRINTLN("");
  }
  return okCnmp && okCmnb;
}

String getModemDetailedStatus(TinyGsm &modem) {
  String rep = "\n========== ESTADO DETALLADO DEL MODEM ==========\n";
  if (!isModemOn()) {
    rep += "Estado Fisico UART : SIN RESPUESTA AT (Apagado o Bloqueado)\n";
    rep += "================================================";
    return rep;
  }

  int simStat = modem.getSimStatus();
  String simStr = (simStat == 1) ? "1 (READY)" :
                  (simStat == 2) ? "2 (PIN LOCKED)" :
                  (simStat == 3) ? "3 (READY)" :
                  String(simStat) + " (NO DETECTADA / ERROR)";

  int16_t csq = modem.getSignalQuality();
  String csqStr = (csq == 99 || csq < 0) ? String(csq) + " (Sin senal)" :
                  String(csq) + " (" + String(-113 + (csq * 2)) + " dBm)";

  int creg = queryRegStatusCode(modem, "+CREG?", "+CREG:");
  int cgreg = queryRegStatusCode(modem, "+CGREG?", "+CGREG:");
  int cereg = queryRegStatusCode(modem, "+CEREG?", "+CEREG:");
  String cpsi = queryCPSI_Line(modem);
  refreshDetectedTechFromCPSI(cpsi);

  bool netConn = modem.isNetworkConnected();
  bool gprsConn = modem.isGprsConnected();
  String ipStr = gprsConn ? modem.localIP().toString() : "0.0.0.0";

  rep += "Estado Fisico UART : OK (115200 bps)\n";
  rep += "Nivel Debug Modem  : " + String(modemDebugLevel) + "\n";
  rep += "Modo Configurado   : " + getModemTechModeName(modemTechMode) + "\n";
  rep += "Tecnologia Activa  : " + getGSMTech() + "\n";
  rep += "Tarjeta SIM (+CPIN): " + simStr + "\n";
  rep += "APN Configurado    : " + apn + "\n";
  rep += "Senal (+CSQ)       : " + csqStr + "\n";
  rep += "Info Celda (+CPSI) : " + cpsi + "\n";
  rep += "Registro GSM CREG  : " + decodeRegStat(creg) + "\n";
  rep += "Registro 2G CGREG  : " + decodeRegStat(cgreg) + "\n";
  rep += "Registro LTE CEREG : " + decodeRegStat(cereg) + "\n";
  rep += "Red Celular        : " + String(netConn ? "CONECTADA" : "DESCONECTADA") + "\n";
  rep += "Sesion Datos GPRS  : " + String(gprsConn ? "ACTIVA" : "INACTIVA") + "\n";
  rep += "IP Asignada        : " + ipStr + "\n";
  rep += "================================================";
  return rep;
}

// ==================== Control de Hardware y Energia ====================

void modemPowerOn() {
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(1000);  // Datasheet Ton = 1S
  digitalWrite(PWR_PIN, LOW);
}

void modemPowerOff() {
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(1500);  // Datasheet Toff = 1.2S
  digitalWrite(PWR_PIN, LOW);
}

void modemRestart() {
  DVL_PRINTLN("[MODEM] Iniciando reinicio de hardware...");
  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] Apagando modem...");
    modemPowerOff();
    delay(2000);
  } else {
    DVL_PRINTLN("[MODEM] El modem no respondia, omitiendo apagado seguro.");
  }
  asegurarModemEncendido();
  notifyModemReset();
}

bool isModemOn() {
  // Limpiamos el buffer del puerto serie
  while (SerialAT.available()) {
    SerialAT.read();
  }

  // Hacemos hasta 3 intentos rápidos de enviar AT
  for (int i = 0; i < 3; i++) {
    if (modemDebugLevel >= 2) {
      DVL_PRINTF("[AT >> %lu ms] AT (sondeo #%d)\n", millis(), i + 1);
    }
    SerialAT.println("AT");
    unsigned long start = millis();
    String response = "";
    while (millis() - start < 400) {
      if (SerialAT.available()) {
        char c = SerialAT.read();
        response += c;
        if (response.indexOf("OK") != -1) {
          if (modemDebugLevel >= 2) {
            DVL_PRINTF("[AT << %lu ms] OK\n", millis());
          }
          return true;
        }
      }
    }
    delay(100);
  }
  return false;
}

void asegurarModemEncendido() {
  DVL_PRINTLN("[MODEM] Verificando estado del modem...");
  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] El modem ya responde (esta encendido).");
    return;
  }

  DVL_PRINTLN("[MODEM] El modem no responde. Intentando encender (Toggle 1)...");
  modemPowerOn();

  // Esperar a que arranque y verificar
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (isModemOn()) {
      DVL_PRINTLN("[MODEM] Modem encendido exitosamente (Toggle 1).");
      return;
    }
    delay(500);
  }

  DVL_PRINTLN("[MODEM] Sigue sin responder. Posiblemente estaba encendido pero colgado y el primer Toggle lo apago. Intentando Toggle 2...");
  modemPowerOn();

  start = millis();
  while (millis() - start < 5000) {
    if (isModemOn()) {
      DVL_PRINTLN("[MODEM] Modem encendido exitosamente (Toggle 2).");
      return;
    }
    delay(500);
  }

  DVL_PRINTLN("[MODEM] ERROR: No se pudo establecer comunicacion con el modem.");
}

void initSD() {
  DVL_PRINTLN("========SDCard Detect.======");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
  if (!SD.begin(SD_CS)) {
    DVL_PRINTLN("SDCard MOUNT FAIL");
  } else {
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    String str = "SDCard Size: " + String(cardSize) + "MB";
    DVL_PRINTLN(str);
  }
}

void printModemInfo(TinyGsm &modem, String &res) {
  modem.sendAT("+SIMCOMATI");
  modem.waitResponse(1000L, res);
  DVL_PRINTLN(res);
  res = "";

  modem.sendAT("+CNMP?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";

  modem.sendAT("+CMNB?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";

  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";
}

// ==================== Sincronizacion de Reloj NTP ====================

bool updateClockFromNTP(TinyGsm &modem) {
  String res;
  DVL_PRINTLN("[NTP] Solicitando sincronizacion...");
  modem.sendAT("+CNTP=\"pool.ntp.org\",0");
  if (modem.waitResponse(1000L, res) == 1) {
    if (modemDebugLevel >= 1) DVL_PRINTLN("[NTP-DBG] Respuesta +CNTP: " + res);
  }
  res = "";

  // Ejecutamos la peticion NTP asincrona del SIM7000
  modem.sendAT("+CNTP");
  modem.waitResponse(3000L, res);
  res = "";

  delay(2000);  // Espera para que la hora se actualice en el modem

  DVL_PRINTLN("[NTP] Leyendo hora desde el modem...");
  modem.sendAT("+CCLK?");

  if (modem.waitResponse(1000L, res) == 1) {
    if (modemDebugLevel >= 1) DVL_PRINTLN(res);
  }

  int index = res.indexOf("+CCLK:");
  if (index != -1) {
    int start = res.indexOf("\"", index);
    int end = res.indexOf("\"", start + 1);
    if (start != -1 && end != -1) {
      String clockStr = res.substring(start, end + 1);  // ej: "25/06/03,10:28:55+00"
      DVL_PRINTLN("[NTP] Hora obtenida: " + clockStr);
      updateInternalClock(clockStr);
      res = "";
      return true;
    }
  }

  DVL_PRINTLN("[NTP] No se pudo parsear la hora del modem");
  return false;
}

String currentTime = "00/00/00,00:00:00+00";  // formato del modem

void updateInternalClock(String clockString) {
  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  // clockString: "25/06/03,10:28:55-12"
  int yy, MM, dd, hh, mm, ss, tz;
  char tz_sign = '+';
  tz = 0;

  if (sscanf(clockString.c_str(), "\"%2d/%2d/%2d,%2d:%2d:%2d%c%2d", &yy, &MM, &dd, &hh, &mm, &ss, &tz_sign, &tz) >= 6) {
    tm.tm_year = 2000 + yy - 1900;  // Año desde 1900
    tm.tm_mon = MM - 1;             // Mes 0-11
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;

    // Asumimos que mktime trata esta tm como UTC (dado que seteamos el env timezone a 0)
    time_t t = mktime(&tm);

    // Corregir mediante el uso de huso horario devuelto (tz viene expresado en cuartos de hora)
    if (tz > 0) {
      int offsetSecs = tz * 15 * 60;
      if (tz_sign == '+') {
        t -= offsetSecs; // El tiempo local le lleva N horas a UTC -> restamos para volver al UTC absoluto
      } else if (tz_sign == '-') {
        t += offsetSecs; // El tiempo local atrasa N horas a UTC -> sumamos para volver al UTC absoluto
      }
    }

    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);

    DVL_PRINTLN("[RTC] Reloj interno sincronizado a UTC absoluto:");
    DVL_PRINTLN(ctime(&t));
  } else {
    DVL_PRINTLN("[RTC] Error al parsear +CCLK");
  }
}

// ==================== Gestion de Registro en Red Celular ====================

void updateNetworkConnection(TinyGsm &modem) {
  // 1. Inicializacion logica del modem si aun no esta listo
  if (!isModemReady) {
    if (modemDebugLevel >= 1) {
      DVL_PRINTLN("[MODEM-DBG] Ejecutando modem.init()...");
    }
    isModemReady = modem.init();
    if (!isModemReady) {
      initFailCount++;
      DVL_PRINTF("[MODEM] Fallo en modem.init() (intento %d/3)\n", initFailCount);
      if (initFailCount >= 3) {
        DVL_PRINTLN("[MODEM] Superado limite de fallos en init(). Reiniciando hardware del modem...");
        initFailCount = 0;
        modemRestart();
      }
      return;
    }

    initFailCount = 0;
    int simStat = modem.getSimStatus();
    if (modemDebugLevel >= 1) {
      DVL_PRINTF("[MODEM-DBG] modem.init() OK. Estado de SIM (+CPIN): %d\n", simStat);
    }

    if (simStat != 1 && simStat != 3) {
      if (strlen(GSM_PIN) > 0 && simStat == 2) {
        DVL_PRINTLN("[MODEM] Desbloqueando SIM con PIN...");
        modem.simUnlock(GSM_PIN);
      } else {
        DVL_PRINTF("[MODEM] ALERTA: Tarjeta SIM no detectada o no lista (status=%d). Verifique la SIM.\n", simStat);
      }
    }

    // Pre-cargar el APN en el perfil PDP antes del attach LTE (CAT-M1 / NB-IoT)
    preconfigureModemAPN(modem, apn);
    modeConfigured = false;
  }

  // 2. Si ya figuraba conectado, verificar periodicamente (cada 10s) que no haya caido la celda ni la sesion GPRS
  if (isModemConnected) {
    if (millis() - lastCheckConnected >= 10000) {
      lastCheckConnected = millis();
      if (!modem.isNetworkConnected()) {
        DVL_PRINTLN("[MODEM] Se perdio el registro en la red celular! Cerrando sesion MQTT y reactivando busqueda...");
        isModemConnected = false;
        isGprsActive = false;
        modeConfigured = false;
        activeTechDetected = "";
        mqtt.disconnect();
      } else {
        bool gprsNow = modem.isGprsConnected();
        if (isGprsActive && !gprsNow) {
          DVL_PRINTLN("[GPRS] Se perdio el contexto de datos GPRS! Cerrando sesion MQTT para reconectar...");
          mqtt.disconnect();
        }
        isGprsActive = gprsNow;

        if (modemDebugLevel >= 1) {
          String cpsi = queryCPSI_Line(modem);
          refreshDetectedTechFromCPSI(cpsi);
          int16_t csq = modem.getSignalQuality();
          DVL_PRINTF("[MODEM-DBG] Red celular OK (%s) | GPRS=%s | CSQ=%d | CPSI: %s\n",
                     getGSMTech().c_str(), isGprsActive ? "SI" : "NO", csq, cpsi.c_str());
        }
      }
    }
    if (isModemConnected) {
      return;
    }
  }

  // 3. Seleccion de indice segun modo fijo o automatico
  if (modemTechMode == 1) {
    currentModeIndex = 0; // Fijo CAT-M1
  } else if (modemTechMode == 2) {
    currentModeIndex = 1; // Fijo NB-IoT
  } else if (modemTechMode == 3) {
    currentModeIndex = 2; // Fijo 2G
  }

  // 4. Si aun no aplicamos la tecnologia de la ventana actual, la configuramos UNA sola vez
  if (!modeConfigured) {
    DVL_PRINTF("[MODEM] Configurando tecnologia: %s (ventana de busqueda: %lu s)...\n",
               getGSMTech().c_str(), modeWindowTimeout / 1000);
    applyModemRadioTech(modem, currentModeIndex);
    preconfigureModemAPN(modem, apn);
    modeConfigured = true;
    modeWindowStart = millis();
    lastConnectAttempt = millis();
    return;
  }

  // 5. Sondeo no bloqueante cada checkInterval (3 segundos) dentro de la ventana de 45s
  if (millis() - lastConnectAttempt < checkInterval) {
    return;
  }
  lastConnectAttempt = millis();

  unsigned long elapsedMs = millis() - modeWindowStart;
  unsigned long elapsedSec = elapsedMs / 1000;
  unsigned long totalWindowSec = modeWindowTimeout / 1000;

  isModemConnected = modem.isNetworkConnected();

  if (modemDebugLevel >= 1) {
    int16_t csq = modem.getSignalQuality();
    int regStat = (currentModeIndex == 2)
                      ? queryRegStatusCode(modem, "+CGREG?", "+CGREG:")
                      : queryRegStatusCode(modem, "+CEREG?", "+CEREG:");
    String cpsi = queryCPSI_Line(modem);
    refreshDetectedTechFromCPSI(cpsi);

    DVL_PRINTF("[MODEM-DBG] [%lus/%lus] Tech: %s | Reg: %s | CSQ: %d | CPSI: %s | Conectado: %s\n",
               elapsedSec, totalWindowSec, getGSMTech().c_str(),
               decodeRegStat(regStat).c_str(), csq, cpsi.c_str(),
               isModemConnected ? "SI" : "NO");
  } else {
    DVL_PRINTF("Modo conexion: %s (%lus/%lus) - Conectado? %s\n",
               getGSMTech().c_str(), elapsedSec, totalWindowSec,
               isModemConnected ? "SI" : "NO");
  }

  if (isModemConnected) {
    String cpsi = queryCPSI_Line(modem);
    refreshDetectedTechFromCPSI(cpsi);
    failedFullCycles = 0;
    lastCheckConnected = millis();
    digitalWrite(LED_PIN, HIGH);
    DVL_PRINTLN("[MODEM] Conectado exitosamente a la red movil (" + getGSMTech() + ").");
    if (modemDebugLevel >= 1) {
      DVL_PRINTLN(getModemDetailedStatus(modem));
    }
    return;
  }

  // 6. Si se cumplio la ventana de espera (45s) sin lograr registro, rotar o reiniciar
  if (elapsedMs >= modeWindowTimeout) {
    modeConfigured = false;

    if (modemTechMode == 0) {
      currentModeIndex = (currentModeIndex + 1) % 3;
      DVL_PRINTLN("[MODEM] Ventana agotada sin registro. Cambiando a siguiente tecnologia: " + getGSMTech());

      if (currentModeIndex == 0) {
        failedFullCycles++;
        DVL_PRINTF("[MODEM] Ronda completa de tecnologias sin exito (%d/2).\n", failedFullCycles);
        if (failedFullCycles >= 2) {
          DVL_PRINTLN("[MODEM] 2 rondas completas sin registro celular. Reiniciando hardware del modem...");
          failedFullCycles = 0;
          modemRestart();
        }
      }
    } else {
      failedFullCycles++;
      DVL_PRINTF("[MODEM] Ventana agotada en modo fijo %s (intento %d/4).\n",
                 getGSMTech().c_str(), failedFullCycles);
      if (failedFullCycles >= 4) {
        DVL_PRINTLN("[MODEM] Multiples ventanas sin registro en modo fijo. Reiniciando hardware del modem...");
        failedFullCycles = 0;
        modemRestart();
      }
    }
  }
}

String getGSMTech() {
  if (isModemConnected && activeTechDetected.length() > 0) {
    return activeTechDetected;
  }
  if (currentModeIndex == 0) return "LTE CAT-M1 (eMTC)";
  if (currentModeIndex == 1) return "NB-IoT";
  if (currentModeIndex == 2) return "2G (GSM/GPRS/EDGE)";
  return "UNKNOWN";
}

void getModemSignalInfo(TinyGsm &modem, String &rsrq, String &rsrp, String &rssi) {
  rsrq = "N/A"; rsrp = "N/A"; rssi = "N/A";
  String res;
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    int index = res.indexOf("+CPSI:");
    if (index != -1) {
      int startPos = index + 6;
      int nextCRLF = res.indexOf("\r", startPos);
      if (nextCRLF != -1) {
        res = res.substring(startPos, nextCRLF);
      } else {
        res = res.substring(startPos);
      }
      res.trim();
      refreshDetectedTechFromCPSI(res);

      String parts[16];
      int count = 0;
      int pos = 0;
      int length = res.length();
      while (pos < length && count < 16) {
        int commaPos = res.indexOf(',', pos);
        if (commaPos == -1) {
          parts[count++] = res.substring(pos);
          break;
        } else {
          parts[count++] = res.substring(pos, commaPos);
          pos = commaPos + 1;
        }
      }

      if (parts[0].indexOf("LTE") != -1 || parts[0].indexOf("CAT-M1") != -1 || parts[0].indexOf("NB-IoT") != -1) {
        if (count >= 13) {
          rsrq = parts[count - 4];
          rsrp = parts[count - 3];
          rssi = parts[count - 2];
        }
      } else if (parts[0].indexOf("GSM") != -1) {
        if (count >= 7) {
          rssi = parts[6];
        }
      }
    }
  }
}

void iniciarSerialModem() {
  DVL_PRINTLN("[MODEM] Inicializando puerto serial (Auto-recuperacion)...");

  // Intentamos primero a la velocidad estandar de 115200
  SerialAT.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(100);

  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] Comunicacion establecida a 115200 bps.");
    return;
  }

  // Si no responde, es muy probable que haya quedado configurado a 57600 bps.
  // Probamos a 57600 bps para restaurarlo.
  DVL_PRINTLN("[MODEM] No responde a 115200. Probando a 57600 para recuperacion...");
  SerialAT.end();
  delay(100);
  SerialAT.begin(57600, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(100);

  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] Encontrado a 57600. Reconfigurando modem a 115200 bps...");
    // Enviamos el comando para restaurar el baud rate a 115200 en el modem
    SerialAT.println("AT+IPR=115200");
    delay(200);
    SerialAT.println("AT&W");
    delay(200);

    // Ahora cambiamos la velocidad del ESP32 a 115200
    SerialAT.end();
    delay(100);
    SerialAT.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    delay(100);

    if (isModemOn()) {
      DVL_PRINTLN("[MODEM] Modem recuperado y verificado a 115200 bps.");
    } else {
      DVL_PRINTLN("[MODEM] ERROR: No responde a 115200 bps luego de reconfigurar.");
    }
  } else {
    DVL_PRINTLN("[MODEM] ERROR: El modem tampoco responde a 57600 bps.");
    // Devolvemos al baud rate estandar por si acaso
    SerialAT.end();
    delay(100);
    SerialAT.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  }
}
