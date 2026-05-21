#include "GPRS.h"
#include "Debug.h"
#include <time.h>
#include <sys/time.h>


#define GSM_PIN ""

// Estados internos para el manejo de red
bool isModemReady = false;
bool isModemConnected = false;
unsigned long lastConnectAttempt = 0;
const unsigned long connectInterval = 10000;  // 10 segundos
int currentModeIndex = 0;
uint8_t networkModes[] = { 38, 39, 13 };  // CAT-M1, NB-IoT, GPRS

static time_t internalTime = 0;


#define SerialAT Serial1

void modemPowerOn() {
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(1000);  //Datasheet Ton mintues = 1S
  digitalWrite(PWR_PIN, LOW);
}

void modemPowerOff() {
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(1500);  //Datasheet Ton mintues = 1.2S
  digitalWrite(PWR_PIN, LOW);
}

void modemRestart() {
  DVL_PRINTLN("[MODEM] Iniciando reinicio de hardware...");
  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] Apagando módem...");
    modemPowerOff();
    delay(2000);
  } else {
    DVL_PRINTLN("[MODEM] El módem no respondía, omitiendo apagado seguro.");
  }
  asegurarModemEncendido();
}

bool isModemOn() {
  // Limpiamos el buffer del puerto serie
  while (SerialAT.available()) {
    SerialAT.read();
  }

  // Hacemos hasta 3 intentos rápidos de enviar AT
  for (int i = 0; i < 3; i++) {
    SerialAT.println("AT");
    unsigned long start = millis();
    String response = "";
    while (millis() - start < 400) {
      if (SerialAT.available()) {
        char c = SerialAT.read();
        response += c;
        if (response.indexOf("OK") != -1) {
          return true;
        }
      }
    }
    delay(100);
  }
  return false;
}

void asegurarModemEncendido() {
  DVL_PRINTLN("[MODEM] Verificando estado del módem...");
  if (isModemOn()) {
    DVL_PRINTLN("[MODEM] El módem ya responde (está encendido).");
    return;
  }

  DVL_PRINTLN("[MODEM] El módem no responde. Intentando encender (Toggle 1)...");
  modemPowerOn();

  // Esperar a que arranque y verificar
  unsigned long start = millis();
  while (millis() - start < 4500) {
    if (isModemOn()) {
      DVL_PRINTLN("[MODEM] Módem encendido exitosamente (Toggle 1).");
      return;
    }
    delay(500);
  }

  DVL_PRINTLN("[MODEM] Sigue sin responder. Posiblemente estaba encendido pero colgado y el primer Toggle lo apagó. Intentando Toggle 2...");
  modemPowerOn();

  start = millis();
  while (millis() - start < 4500) {
    if (isModemOn()) {
      DVL_PRINTLN("[MODEM] Módem encendido exitosamente (Toggle 2).");
      return;
    }
    delay(500);
  }

  DVL_PRINTLN("[MODEM] ERROR: No se pudo establecer comunicación con el módem.");
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

  //"========SIMCOMATI======"
  modem.sendAT("+SIMCOMATI");
  modem.waitResponse(1000L, res);
  DVL_PRINTLN(res);
  res = "";

  //=====Preferred mode selection====="
  modem.sendAT("+CNMP?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";

  //=====Preferred selection between CAT-M and NB-IoT====="
  modem.sendAT("+CMNB?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";

  //=====Inquiring UE system information====="
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";
}

bool updateClockFromNTP(TinyGsm &modem) {
  String res;
  DVL_PRINTLN("[NTP] Solicitando sincronizacion...");
  modem.sendAT("AT+CNTP=\"pool.ntp.org\",0");
  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }
  res = "";

  delay(2000);  // Espera para que la hora se actualice en el modem

  DVL_PRINTLN("[NTP] Leyendo hora desde el modem...");
  modem.sendAT("+CCLK?");

  if (modem.waitResponse(1000L, res) == 1) {
    DVL_PRINTLN(res);
  }

  int index = res.indexOf("+CCLK:");
  if (index != -1) {
    int start = res.indexOf("\"", index);
    int end = res.indexOf("\"", start + 1);
    if (start != -1 && end != -1) {
      String clockStr = res.substring(start, end + 1);  // ej: "25/06/03,10:28:55+00"
      DVL_PRINTLN("[NTP] Hora obtenida:");
      DVL_PRINTLN(clockStr);
      updateInternalClock(clockStr);
      res = "";
      return true;
    }
  } else {

    DVL_PRINTLN("[NTP] No se pudo parsear la hora del modem");
    return false;
  }
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

void updateNetworkConnection(TinyGsm &modem) {

  //Inicializo el modem

  if (!isModemReady) {
    isModemReady = modem.init();
    if (!isModemReady) {
      DVL_PRINTLN("Modem init failed");
      return;
    }
  }

  if (strlen(GSM_PIN) > 0 && modem.getSimStatus() != 3) {
    modem.simUnlock(GSM_PIN);
  }

  if (isModemConnected || millis() - lastConnectAttempt < connectInterval) {
    return;
  }

  lastConnectAttempt = millis();
  modem.setNetworkMode(networkModes[currentModeIndex]);
  delay(1000);
  isModemConnected = modem.isNetworkConnected();

  DVL_PRINT("Modo conexion: ");
  DVL_PRINT(getGSMTech());
  DVL_PRINT(" - Conectado? ");
  DVL_PRINTLN(isModemConnected ? "SI" : "NO");

  if (!isModemConnected) {
    currentModeIndex = (currentModeIndex + 1) % 3;  // Cambia al siguiente modo
  } else {
    digitalWrite(LED_PIN, HIGH);  // LED ON si conecta
  }
}

String getGSMTech() {
  if (currentModeIndex >= 0 && currentModeIndex < 3) {
    uint8_t mode = networkModes[currentModeIndex];
    if (mode == 38) return "LTE CAT-M1 (eMTC)";
    if (mode == 39) return "NB-IoT";
    if (mode == 13) return "2G (GSM/GPRS/EDGE)";
  }
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
              rsrq = parts[count-4];
              rsrp = parts[count-3];
              rssi = parts[count-2];
           }
        } else if (parts[0].indexOf("GSM") != -1) {
           if (count >= 7) {
              rssi = parts[6];
           }
        }
    }
  }
}
