#include "GPRS.h"
#include <time.h>
#include <sys/time.h>


#define GSM_PIN ""

// Estados internos para el manejo de red
bool isModemReady = false;
bool isModemConnected = false;
unsigned long lastConnectAttempt = 0;
const unsigned long connectInterval = 10000; // 10 segundos
int currentModeIndex = 0;
uint8_t networkModes[] = {38, 39, 13}; // CAT-M1, NB-IoT, GPRS

static time_t internalTime = 0;


#define SerialAT Serial1

void modemPowerOn()
{
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH);
    delay(1000);    //Datasheet Ton mintues = 1S
    digitalWrite(PWR_PIN, LOW);
}

void modemPowerOff()
{
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH);
    delay(1500);    //Datasheet Ton mintues = 1.2S
    digitalWrite(PWR_PIN, LOW);
}

void modemRestart() {
  modemPowerOff();
  delay(1000);
  modemPowerOn();
}

void initSD() {
  Serial.println("========SDCard Detect.======");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println("SDCard MOUNT FAIL");
  } else {
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    String str = "SDCard Size: " + String(cardSize) + "MB";
    Serial.println(str);
  }
}

void printModemInfo(TinyGsm &modem, String &res) {
  
  //"========SIMCOMATI======"
  modem.sendAT("+SIMCOMATI");
  modem.waitResponse(1000L, res);
  Serial.println(res);
  res = "";

  //=====Preferred mode selection====="
  modem.sendAT("+CNMP?");
  if (modem.waitResponse(1000L, res) == 1) {
    Serial.println(res);
  }
  res = "";
  
  //=====Preferred selection between CAT-M and NB-IoT====="
  modem.sendAT("+CMNB?");
  if (modem.waitResponse(1000L, res) == 1) {
    Serial.println(res);
  }
  res = "";

  //=====Inquiring UE system information====="
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    Serial.println(res);
  }
  res = "";

}






bool updateClockFromNTP(TinyGsm &modem) {
  String res;
  Serial.println("[NTP] Solicitando sincronización...");
  modem.sendAT("AT+CNTP=\"pool.ntp.org\",0");
  if (modem.waitResponse(1000L, res) == 1) {
    Serial.println(res);
  }
  res = "";
    
  delay(2000); // Espera para que la hora se actualice en el módem

  Serial.println("[NTP] Leyendo hora desde el módem...");
  modem.sendAT("+CCLK?");
  
  if (modem.waitResponse(1000L, res) == 1) {
    Serial.println(res);
  }
  
  int index = res.indexOf("+CCLK:");
  if (index != -1) {
    int start = res.indexOf("\"", index);
    int end = res.indexOf("\"", start + 1);
    if (start != -1 && end != -1) {
      String clockStr = res.substring(start, end + 1);  // ej: "25/06/03,10:28:55+00"
      Serial.println("[NTP] Hora obtenida:");
      Serial.println(clockStr);
      updateInternalClock(clockStr);
      res = "";
      return true;
    }
  }else{

  Serial.println("[NTP] No se pudo parsear la hora del módem");
  return false;
}
}
String currentTime = "00/00/00,00:00:00+00";  // formato del módem

void updateInternalClock(String clockString) {
  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  // clockString: "25/06/03,10:28:55+00"
  int yy, MM, dd, hh, mm, ss;
  if (sscanf(clockString.c_str(), "\"%2d/%2d/%2d,%2d:%2d:%2d", &yy, &MM, &dd, &hh, &mm, &ss) == 6) {
    tm.tm_year = 2000 + yy - 1900;  // Año desde 1900
    tm.tm_mon  = MM - 1;            // Mes 0-11
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = ss;

    time_t t = mktime(&tm);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);

    Serial.println("[RTC] Reloj interno actualizado:");
    Serial.println(ctime(&t));
  } else {
    Serial.println("[RTC] Error al parsear +CCLK");
  }
}





void updateNetworkConnection(TinyGsm &modem) {
 
//Inicializo el modem

  if (!isModemReady) {
    isModemReady = modem.init();
    if (!isModemReady) {
      Serial.println("Modem init failed");
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

  Serial.print("Modo conexión: ");
  Serial.print(networkModes[currentModeIndex]);
  Serial.print(" - Conectado? ");
  Serial.println(isModemConnected ? "SI" : "NO");

  if (!isModemConnected) {
    currentModeIndex = (currentModeIndex + 1) % 3; // Cambia al siguiente modo
  } else {
    digitalWrite(LED_PIN, HIGH); // LED ON si conecta
  }
}
