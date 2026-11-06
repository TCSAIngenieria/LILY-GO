#include "ClockSync.h"
#include "GPRS.h"
#include "Debug.h"
#include <sys/time.h>

enum ClockSource : uint8_t { CLOCK_NONE, CLOCK_NETWORK, CLOCK_GPS, CLOCK_NTP };
static ClockSource clockSource = CLOCK_NONE;
static String networkTimeLine;
static unsigned long networkTimeReceived = 0;
static String ntpResultLine;
static unsigned long ntpResultReceived = 0;
static bool ntpPending = false;
static bool ntpAttempted = false;
static bool ntpSucceeded = false;
static unsigned long ntpStarted = 0;
static unsigned long ntpLastAttempt = 0;
static unsigned long ntpLastSuccess = 0;

void clockTimeNotice(const String &line) {
  if (line.startsWith("*PSUTTZ:")) {
    networkTimeLine = line;
    networkTimeReceived = millis();
  } else if (line.startsWith("+CNTP:")) {
    ntpResultLine = line;
    ntpResultReceived = millis();
  }
}

void clockModemReset() {
  ntpPending = false;
  ntpResultLine = "";
}

static bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool validUtc(int year, int month, int day, int hour, int minute, int second) {
  if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) return false;
  const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int maxDay = monthDays[month - 1] + (month == 2 && leapYear(year) ? 1 : 0);
  return day >= 1 && day <= maxDay;
}

// Dias desde 1970-01-01 sin depender de TZ ni de mktime().
static int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yoe = year - era * 400;
  const int adjustedMonth = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned doy = (153 * adjustedMonth + 2) / 5 + day - 1;
  const unsigned yoeDays = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + yoeDays - 719468;
}

static bool setClockUtc(int year, int month, int day, int hour, int minute,
                        int second, ClockSource source, unsigned long receivedAt) {
  if (!validUtc(year, month, day, hour, minute, second)) return false;
  if (source < clockSource) return true; // La red no reemplaza un GPS/NTP valido.

  int64_t epoch = daysFromCivil(year, month, day) * 86400LL +
                  hour * 3600 + minute * 60 + second;
  epoch += (unsigned long)(millis() - receivedAt) / 1000UL;
  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(epoch);
  if (settimeofday(&tv, nullptr) != 0) return false;
  clockSource = source;
  const char *sourceName = source == CLOCK_NTP ? "NTP" :
                           source == CLOCK_GPS ? "GPS" : "RED";
  DVL_PRINTF("[RTC] UTC sincronizado desde %s: %04d-%02d-%02d %02d:%02d:%02d\n",
             sourceName, year, month, day, hour, minute, second);
  return true;
}

static bool parseDigits(const String &value, unsigned pos, unsigned count, int &out) {
  if (value.length() < pos + count) return false;
  out = 0;
  for (unsigned i = 0; i < count; ++i) {
    char c = value[pos + i];
    if (c < '0' || c > '9') return false;
    out = out * 10 + c - '0';
  }
  return true;
}

// Acepta yy/MM/dd,hh:mm:ss o yyyy/MM/dd,hh:mm:ss (ambos UTC).
static bool parseSlashUtc(const String &value, bool shortYear, int &year, int &month,
                          int &day, int &hour, int &minute, int &second) {
  unsigned yearWidth = shortYear ? 2 : 4;
  if (value.length() < yearWidth + 15 || value[yearWidth] != '/' ||
      value[yearWidth + 3] != '/' || value[yearWidth + 6] != ',' ||
      value[yearWidth + 9] != ':' || value[yearWidth + 12] != ':') return false;
  if (!parseDigits(value, 0, yearWidth, year) ||
      !parseDigits(value, yearWidth + 1, 2, month) ||
      !parseDigits(value, yearWidth + 4, 2, day) ||
      !parseDigits(value, yearWidth + 7, 2, hour) ||
      !parseDigits(value, yearWidth + 10, 2, minute) ||
      !parseDigits(value, yearWidth + 13, 2, second)) return false;
  if (shortYear) year += 2000;
  return validUtc(year, month, day, hour, minute, second);
}

static bool parseNetworkUtc(const String &line, int &year, int &month, int &day,
                            int &hour, int &minute, int &second) {
  String value = line.substring(8);
  value.trim();
  // Forma observada en la SIM7000: 26/11/05,17:55:45","-12",0.
  if (value.length() > 17 && value[17] == '"' &&
      parseSlashUtc(value, true, year, month, day, hour, minute, second)) return true;

  // Forma documentada: 2026,11,05,17,55,45,"-12",0.
  int consumed = 0;
  if (sscanf(value.c_str(), "%4d,%2d,%2d,%2d,%2d,%2d%n",
             &year, &month, &day, &hour, &minute, &second, &consumed) == 6 &&
      consumed > 0 && value.length() > (unsigned)consumed &&
      value[consumed] == ',' && validUtc(year, month, day, hour, minute, second)) {
    return true;
  }
  return false;
}

static bool parseNtpUtc(const String &line, int &year, int &month, int &day,
                        int &hour, int &minute, int &second) {
  String result = line.substring(line.indexOf(':') + 1);
  result.trim();
  if (!result.startsWith("1,")) return false;
  String value = result.substring(2);
  value.trim();
  if (value.startsWith("\"")) value.remove(0, 1);
  if (value.endsWith("\"")) value.remove(value.length() - 1);
  return value.length() == 19 &&
         parseSlashUtc(value, false, year, month, day, hour, minute, second);
}

bool setClockFromGpsUtc(int year, int month, int day, int hour, int minute, int second) {
  if (!setClockUtc(year, month, day, hour, minute, second, CLOCK_GPS, millis())) {
    DVL_PRINTLN("[GPS] Fecha UTC del fix invalida; reloj sin modificar.");
    return false;
  }
  return true;
}

void serviceClockFromModem(TinyGsm &modem, bool dataConnected) {
  // TinyGSM procesa los URC de socket; el interceptor conserva los de hora.
  if (isModemReady && modemStream.available()) modem.waitResponse(5L);

  int year, month, day, hour, minute, second;
  if (networkTimeLine.length()) {
    String line = networkTimeLine;
    networkTimeLine = "";
    if (millis() - networkTimeReceived > 300000UL ||
        !parseNetworkUtc(line, year, month, day, hour, minute, second) ||
        !setClockUtc(year, month, day, hour, minute, second,
                     CLOCK_NETWORK, networkTimeReceived)) {
      DVL_PRINTLN("[RTC] Aviso *PSUTTZ invalido; reloj sin modificar.");
    }
  }

  if (ntpResultLine.length()) {
    String line = ntpResultLine;
    ntpResultLine = "";
    if (ntpPending && millis() - ntpStarted < 45000UL &&
        millis() - ntpResultReceived < 45000UL) {
      ntpPending = false;
      if (parseNtpUtc(line, year, month, day, hour, minute, second) &&
          setClockUtc(year, month, day, hour, minute, second,
                       CLOCK_NTP, ntpResultReceived)) {
        ntpSucceeded = true;
        ntpLastSuccess = millis();
      } else {
        DVL_PRINTLN("[NTP] Sin hora UTC valida: " + line);
      }
    }
  }

  if (ntpPending && millis() - ntpStarted >= 45000UL) {
    ntpPending = false;
    DVL_PRINTLN("[NTP] Sin respuesta final; se reintentara.");
  }
  if (!dataConnected || ntpPending) return;

  const unsigned long retryInterval = 60000UL;
  const unsigned long successInterval = 8UL * 60UL * 60UL * 1000UL;
  if (ntpSucceeded && millis() - ntpLastSuccess < successInterval) return;
  if (ntpAttempted && millis() - ntpLastAttempt < retryInterval) return;
  ntpAttempted = true;
  ntpLastAttempt = millis();

  // Modo 1 entrega la hora UTC en +CNTP sin cambiar el reloj local +CCLK.
  modem.sendAT("+CNTP=\"pool.ntp.org\",0,1,1");
  if (modem.waitResponse(3000L) != 1) {
    DVL_PRINTLN("[NTP] No se pudo configurar el servidor UTC.");
    return;
  }
  ntpResultLine = "";
  ntpPending = true;
  ntpStarted = millis();
  modem.sendAT("+CNTP");
  if (modem.waitResponse(3000L) != 1) {
    ntpPending = false;
    DVL_PRINTLN("[NTP] El modem rechazo la solicitud.");
    return;
  }
  DVL_PRINTLN("[NTP] Solicitud enviada; esperando resultado final +CNTP.");
}
