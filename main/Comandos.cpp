#include "Comandos.h"
#include "Debug.h"
#include "FOTA.h"
#include "Flash.h"
#include "GNSS.h" 
#include <Preferences.h>
#include "MQTT.h"

extern Preferences preferences;

String startMarker = "DATA,";
String endMarker = "\r";
extern String versionado;
extern String ident;
extern String topic1;
char separator = ',';

uint en_sensor = 0;
uint en_serial = 0;
uint en_modbus = 0;
uint en_ble = 0;
uint en_adc = 0;
uint en_pivot = 0;
unsigned long delay_sirena = 300;
unsigned long siren_duration = 0;
unsigned long deep_sleep_time = 300;
#define PIN_IN_1 13
#define PIN_IN_2 14

extern unsigned long publishInterval;
extern FOTAClass FOTA;

String procesarComando(String comando) {
  comando.trim();
  String respuesta = "";

  if (comando.startsWith("DVL+SLAT=")) {
    String nuevaLat = comando.substring(9);
    guardar_en_flash("lat", nuevaLat);
    setLatitude(nuevaLat);
    setLocationValid(true);
    respuesta = "RLAT_OK";

  } else if (comando.startsWith("DVL+SLONG=")) {
    String nuevaLon = comando.substring(10);
    guardar_en_flash("lon", nuevaLon);
    setLongitude(nuevaLon);
    setLocationValid(true);
    respuesta = "RLONG_OK";

  } else if (comando.startsWith("DVL+STIME=")) {
    String publishInterval_s = comando.substring(10);
    guardar_en_flash("time", publishInterval_s);
    publishInterval = strtoul(publishInterval_s.c_str(), NULL, 10);
    respuesta = "TIEMPO SETEADO OK";

  } else if (comando.startsWith("DVL+ID=")) {
    String ident_s = comando.substring(7);
    guardar_en_flash("ident", ident_s);
    ident = ident_s;
    topic1 = "DVL/LILY-GO/" + ident;
    if (mqtt.connected()) {
      mqtt.disconnect();
    }
    respuesta = "Rident_OK (TOPIC: " + topic1 + ")";

  } else if (comando.startsWith("DVL+FOTA=")) {
    String urlFOTA = comando.substring(9);
    urlFOTA.trim();
    FOTA.startUpdate(urlFOTA);
    preferences.begin("fota", false);
    preferences.putString("url", urlFOTA); 
    preferences.end();
    respuesta = "FOTA_INICIADA";

  } else if (comando == "DVL+VER") {
    respuesta = "VERSION=" + versionado;

  } else if (comando.startsWith("DVL+EN_ADC=")) {
    String v = comando.substring(String("DVL+EN_ADC=").length());
    v.trim();
    en_adc = (v == "1") ? 1 : 0;
    preferences.begin("enables", false);
    preferences.putUInt("adc", en_adc);
    preferences.end();
    respuesta = (en_adc == 1) ? ">> HABILITADO REPORTE ADC" : ">> DESHABILITADO REPORTE ADC";

  } else if (comando == "DVL+RESET") {
    respuesta = ">> Reiniciando dispositivo...";
    DVL_PRINTLN(respuesta);
    delay(100); 
    ESP.restart();

  } else if (comando == "DVL+QTIME") {
    respuesta = "TIME=" + String(publishInterval);

  } else if (comando == "DVL+QLAT") {
    respuesta = "LAT=" + ultimaLat;

  } else if (comando == "DVL+QLONG") {
    respuesta = "LONG=" + ultimaLon;

  } else if (comando == "DVL+QEN_ADC") {
    preferences.begin("enables", true);
    en_adc = preferences.getUInt("adc", 0);
    preferences.end();
    respuesta = "EN_ADC=" + String(en_adc);

  } else if (comando == "DVL+PIVON") {
    if (en_pivot == 1) {
      respuesta = "[ERR] el sistema ya esta activado. Primero debe desactivarlo con PIVOFF";
    } else if (digitalRead(PIN_IN_1) == digitalRead(PIN_IN_2)) {
      respuesta = "[ERR] se debe revisar la instalacion";
    } else {
      en_pivot = 1;
      preferences.begin("enables", false);
      preferences.putUInt("pivot", 1);
      preferences.end();
      respuesta = ">> SISTEMA PIVOT HABILITADO";
    }

  } else if (comando == "DVL+PIVOFF") {
    en_pivot = 0;
    preferences.begin("enables", false);
    preferences.putUInt("pivot", 0);
    preferences.end();
    respuesta = ">> SISTEMA PIVOT DESHABILITADO (sirena apagada)";

  } else if (comando.startsWith("DVL+SDSIR=")) {
    String sdsir_s = comando.substring(10);
    delay_sirena = strtoul(sdsir_s.c_str(), NULL, 10);
    preferences.begin("device", false);
    preferences.putULong("dsir", delay_sirena);
    preferences.end();
    respuesta = "RETRASO SIRENA SETEADO OK (segundos): " + String(delay_sirena);

  } else if (comando == "DVL+QDSIR") {
    respuesta = "DSIR=" + String(delay_sirena);

  } else if (comando.startsWith("DVL+STSIR=")) {
    String stsir_s = comando.substring(10);
    siren_duration = strtoul(stsir_s.c_str(), NULL, 10);
    preferences.begin("device", false);
    preferences.putULong("tsir", siren_duration);
    preferences.end();
    respuesta = "DURACION SIRENA SETEADO OK (segundos): " + String(siren_duration);

  } else if (comando == "DVL+QTSIR") {
    respuesta = "TSIR=" + String(siren_duration);

  } else if (comando.startsWith("DVL+SDEEP=")) {
    String sdeep_s = comando.substring(10);
    deep_sleep_time = strtoul(sdeep_s.c_str(), NULL, 10);
    preferences.begin("device", false);
    preferences.putULong("ds_time", deep_sleep_time);
    preferences.end();
    respuesta = "DEEP SLEEP TIME SETEADO OK (segundos): " + String(deep_sleep_time);

  } else if (comando == "DVL+QDEEP") {
    respuesta = "DEEP=" + String(deep_sleep_time);

  } else if (comando == "DVL+QPIV") {
    preferences.begin("enables", true);
    en_pivot = preferences.getUInt("pivot", 0);
    preferences.end();
    respuesta = "PIVOT=" + String(en_pivot);

  } else if (comando == "DVL+PASSON") {
    extern bool passthroughMode;
    passthroughMode = true;
    respuesta = "MODO PASSTHROUGH ACTIVADO";

  } else {
    respuesta = "[ERR] Comando no reconocido o deshabilitado.";
  }

  Serial.println(respuesta);
  return respuesta;
}

void escucharComandos() {
  while (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();
    if (comando.length() > 0) {
      procesarComando(comando);
    }
  }
}
