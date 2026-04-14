#include "Fechayhora.h"
#include "Debug.h"
#include <time.h>
#include <sys/time.h>



void updateClockFromNTP_wifi() {
  const char* ntpServer = "pool.ntp.org";
  //const long  gmtOffset_sec = -10800;  // Argentina = UTC -3

  const long gmtOffset_sec = 0;  // UTC 0

  const int daylightOffset_sec = 0;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  int intentos = 0;
  const int maxIntentos = 30;  // 30 * 200ms = 6 segundos max

  while (!getLocalTime(&timeinfo) && intentos < maxIntentos) {
    delay(10);             // esperar un poco entre intentos
    esp_task_wdt_reset();  // mantener vivo el watchdog
    intentos++;
  }

  if (intentos >= maxIntentos) {
    DVL_PRINTLN("[NTP]  Fallo la sincronizacion tras varios intentos");
    return;
  }

  time_t now = mktime(&timeinfo);
  struct timeval tv = { .tv_sec = now };
  settimeofday(&tv, NULL);

  DVL_PRINTLN("[NTP]  Sincronizacion exitosa");
  DVL_PRINTLN(ctime(&now));
  esp_task_wdt_reset();  // por las dudas, una mas
}

String printCurrentTime() {
  time_t now;
  time(&now);
  if (now < 10000) {  // Validar si la hora aun no se inicializo
    DVL_PRINTLN("⏱️ No se pudo obtener la hora.");
    return "";
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}
