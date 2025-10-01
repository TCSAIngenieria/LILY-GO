#include "Fechayhora.h"
#include <time.h>
#include <sys/time.h>



void updateClockFromNTP_wifi() {
  const char* ntpServer = "pool.ntp.org";
  //const long  gmtOffset_sec = -10800;  // Argentina = UTC -3

  const long  gmtOffset_sec = 0;  // UTC 0

  const int   daylightOffset_sec = 0;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  int intentos = 0;
  const int maxIntentos = 30;  // 30 * 200ms = 6 segundos max

  while (!getLocalTime(&timeinfo) && intentos < maxIntentos) {
    delay(10);                 // esperar un poco entre intentos
    esp_task_wdt_reset();       // mantener vivo el watchdog
    intentos++;
  }

  if (intentos >= maxIntentos) {
    Serial.println("[NTP]  Fallo la sincronizacion tras varios intentos");
    return;
  }

  time_t now = mktime(&timeinfo);
  struct timeval tv = { .tv_sec = now };
  settimeofday(&tv, NULL);

  Serial.println("[NTP]  Sincronizacion exitosa");
  Serial.println(ctime(&now));
  esp_task_wdt_reset();  // por las dudas, una mas
}




String printCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {  // Timeout de 100 ms
    Serial.println("⏱️ No se pudo obtener la hora local.");
    return "";
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}
