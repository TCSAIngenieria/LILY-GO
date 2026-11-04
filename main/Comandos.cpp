#include "Comandos.h"
#include "Debug.h"
#include "FOTA.h"
#include "Flash.h"
#include "GNSS.h"
#include "GPRS.h"
#include "MQTT.h"
#include <Preferences.h>
#include <WiFi.h>

extern TinyGsm modem;
extern Preferences preferences;

String startMarker = "DATA,";
String endMarker = "\r";
extern String versionado;
extern String ident;
extern String topic1;
char separator = ',';

extern float filterADC[2][2];
extern float paramADC[2][2];
extern int cantMed;
extern uint16_t tADC;

uint en_sensor = 0;
uint en_serial = 0;
uint en_modbus = 0;
uint en_ble = 0;
uint en_adc = 0;
uint en_modem = 1;
uint en_gps = 0;
uint en_wifi = 1;
uint en_pivot = 0;
unsigned long delay_sirena = 300;
unsigned long siren_duration = 0;
unsigned long deep_sleep_time = 300;
#define PIN_IN_1 13
#define PIN_IN_2 14

extern unsigned long publishInterval;
extern FOTAClass FOTA;
extern bool TINY_GSM_USE_WIFI;
extern bool TINY_GSM_USE_GPRS;

String procesarComando(String comando) {
  comando.trim();
  String respuesta = "";

  if (comando.startsWith("DVL+SLAT=")) {
    String nuevaLat = comando.substring(9);
    guardar_en_flash("lat", nuevaLat);
    ultimaLat = nuevaLat;
    setLatitude(nuevaLat);
    setLocationValid(true);
    respuesta = "RLAT_OK";

  } else if (comando.startsWith("DVL+SLONG=")) {
    String nuevaLon = comando.substring(10);
    guardar_en_flash("lon", nuevaLon);
    ultimaLon = nuevaLon;
    setLongitude(nuevaLon);
    setLocationValid(true);
    respuesta = "RLONG_OK";

  } else if (comando.startsWith("DVL+STIME=")) {
    String publishInterval_s = comando.substring(10);
    guardar_en_flash("time", publishInterval_s);
    publishInterval = strtoul(publishInterval_s.c_str(), NULL, 10);
    respuesta = "TIEMPO SETEADO OK";

  } else if (comando.startsWith("DVL+SAPN=")) {
    String nuevoAPN = comando.substring(9);
    nuevoAPN.trim();
    guardar_en_flash("apn", nuevoAPN);
    apn = nuevoAPN;
    if (en_modem && isModemReady) {
      preconfigureModemAPN(modem, apn);
    }
    respuesta = "APN SETEADO OK";

  } else if (comando == "DVL+QAPN") {
    respuesta = "APN=" + apn;

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

  } else if (comando.startsWith("DVL+CFG_FOTA=")) {
    String args = comando.substring(13);
    int commaIdx = args.indexOf(',');
    if (commaIdx != -1) {
      unsigned long timeout = strtoul(args.substring(0, commaIdx).c_str(), NULL, 10);
      int secure = args.substring(commaIdx + 1).toInt();
      if (timeout > 0 && (secure == 0 || secure == 1)) {
        preferences.begin("fota_cfg", false);
        preferences.putULong("timeout", timeout);
        preferences.putInt("secure", secure);
        preferences.end();
        respuesta = ">> FOTA CONFIGURADO: TIMEOUT=" + String(timeout) + "s, SEGURIDAD=" + String(secure);
      } else {
        respuesta = "ERROR: Valores invalidos. Formato: DVL+CFG_FOTA=<segundos>,<0|1>";
      }
    } else {
      respuesta = "ERROR: Formato incorrecto. Formato: DVL+CFG_FOTA=<segundos>,<0|1>";
    }

  } else if (comando == "DVL+QCFG_FOTA") {
    preferences.begin("fota_cfg", true);
    unsigned long timeout = preferences.getULong("timeout", 600);
    int secure = preferences.getInt("secure", 0);
    preferences.end();
    respuesta = "TIMEOUT=" + String(timeout) + " | SECURE=" + String(secure);

  } else if (comando == "DVL+VER") {
    respuesta = "VERSION=" + versionado;

  } else if (comando.startsWith("DVL+EN_MODEM=")) {
    String v = comando.substring(String("DVL+EN_MODEM=").length());
    v.trim();
    en_modem = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("modem", en_modem);
    preferences.end();
    if (en_modem == 1) {
      asegurarModemEncendido();
      forceModemTechReconfig();
      if (en_wifi != 1 || WiFi.status() != WL_CONNECTED) {
        TINY_GSM_USE_WIFI = false;
        TINY_GSM_USE_GPRS = true;
      }
      respuesta = ">> HABILITADO MODEM";
    } else {
      TINY_GSM_USE_WIFI = true;
      TINY_GSM_USE_GPRS = false;
      respuesta = ">> DESHABILITADO MODEM";
    }

  } else if (comando == "DVL+QEN_MODEM") {
    preferences.begin("enables", true);
    en_modem = preferences.getUInt("modem", 1);
    preferences.end();
    respuesta = "EN_MODEM=" + String(en_modem);

  } else if (comando.startsWith("DVL+DBG_MODEM=")) {
    String v = comando.substring(String("DVL+DBG_MODEM=").length());
    v.trim();
    int lvl = v.toInt();
    if (lvl >= 0 && lvl <= 2) {
      modemDebugLevel = (uint8_t)lvl;
      preferences.begin("enables", false);
      preferences.putUInt("modem_dbg", modemDebugLevel);
      preferences.end();
      if (modemDebugLevel == 0) {
        respuesta = ">> DBG_MODEM=0 (MODO NORMAL / RESUMIDO)";
      } else if (modemDebugLevel == 1) {
        respuesta = ">> DBG_MODEM=1 (DIAGNOSTICO DETALLADO DE ESTADOS Y SENAL)";
      } else {
        respuesta = ">> DBG_MODEM=2 (VERBORRAGICO TOTAL + TRAZA COMANDOS AT CRUDOS)";
      }
    } else {
      respuesta = "[ERR] Valor invalido. Use DVL+DBG_MODEM=0, 1 o 2";
    }

  } else if (comando == "DVL+QDBG_MODEM") {
    preferences.begin("enables", true);
    modemDebugLevel = (uint8_t)preferences.getUInt("modem_dbg", 0);
    preferences.end();
    respuesta = "DBG_MODEM=" + String(modemDebugLevel);

  } else if (comando.startsWith("DVL+MODEM_TECH=")) {
    String v = comando.substring(String("DVL+MODEM_TECH=").length());
    v.trim();
    int tech = v.toInt();
    if (tech >= 0 && tech <= 3) {
      modemTechMode = (uint8_t)tech;
      preferences.begin("enables", false);
      preferences.putUInt("modem_tech", modemTechMode);
      preferences.end();
      forceModemTechReconfig();
      respuesta = ">> MODEM_TECH=" + String(modemTechMode) + " (" + getModemTechModeName(modemTechMode) + ")";
    } else {
      respuesta = "[ERR] Valor invalido. Use DVL+MODEM_TECH=0 (Auto), 1 (CAT-M1), 2 (NB-IoT) o 3 (2G)";
    }

  } else if (comando == "DVL+QMODEM_TECH") {
    preferences.begin("enables", true);
    modemTechMode = (uint8_t)preferences.getUInt("modem_tech", 0);
    preferences.end();
    respuesta = "MODEM_TECH=" + String(modemTechMode) + " (" + getModemTechModeName(modemTechMode) + ") | ACTIVA=" + getGSMTech();

  } else if (comando == "DVL+MODEM_STATUS") {
    respuesta = getModemDetailedStatus(modem);

  } else if (comando.startsWith("DVL+EN_GPS=")) {
    String v = comando.substring(String("DVL+EN_GPS=").length());
    v.trim();
    en_gps = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("gps", en_gps);
    preferences.end();
    respuesta = (en_gps == 1) ? ">> HABILITADO GPS" : ">> DESHABILITADO GPS";

  } else if (comando == "DVL+QEN_GPS") {
    preferences.begin("enables", true);
    en_gps = preferences.getUInt("gps", 0);
    preferences.end();
    respuesta = "EN_GPS=" + String(en_gps);

  } else if (comando.startsWith("DVL+EN_WIFI=")) {
    String v = comando.substring(String("DVL+EN_WIFI=").length());
    v.trim();
    en_wifi = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("wifi", en_wifi);
    preferences.end();
    if (en_wifi == 1) {
      WiFi.mode(WIFI_STA);
      TINY_GSM_USE_WIFI = true;
      TINY_GSM_USE_GPRS = false;
      respuesta = ">> HABILITADO WIFI CLIENTE (INTENTANDO CONEXION)";
    } else {
      TINY_GSM_USE_WIFI = false;
      TINY_GSM_USE_GPRS = true;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      respuesta = ">> DESHABILITADO WIFI (USANDO EXCLUSIVAMENTE MODEM)";
    }

  } else if (comando == "DVL+QEN_WIFI") {
    preferences.begin("enables", true);
    en_wifi = preferences.getUInt("wifi", 1);
    preferences.end();
    respuesta = "EN_WIFI=" + String(en_wifi);

  } else if (comando.startsWith("DVL+EN_ADC=")) {
    String v = comando.substring(String("DVL+EN_ADC=").length());
    v.trim();
    en_adc = (v == "1") ? 1 : 0;
    preferences.begin("enables", false);
    preferences.putUInt("adc", en_adc);
    preferences.end();
    respuesta = (en_adc == 1) ? ">> HABILITADO REPORTE ADC" : ">> DESHABILITADO REPORTE ADC";

  } else if (comando == "DVL+QEN_ADC") {
    preferences.begin("enables", true);
    en_adc = preferences.getUInt("adc", 0);
    preferences.end();
    respuesta = "EN_ADC=" + String(en_adc);

  } else if (comando.startsWith("DVL+SFIL=")) {
    String args = comando.substring(9);
    int firstComma = args.indexOf(',');
    int secondComma = args.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      int n = args.substring(0, firstComma).toInt();
      float m = args.substring(firstComma + 1, secondComma).toFloat();
      float p = args.substring(secondComma + 1).toFloat();

      if (n >= 0 && n < 2) {
        filterADC[n][0] = m;
        filterADC[n][1] = p;

        preferences.begin("adc_config", false);
        String keyMin = "fil_" + String(n) + "_0";
        String keyVal = "fil_" + String(n) + "_1";
        preferences.putFloat(keyMin.c_str(), m);
        preferences.putFloat(keyVal.c_str(), p);
        preferences.end();

        respuesta = "FILTRO SETEADO OK";
      } else {
        respuesta = "ERROR: Indice fuera de rango (0-1)";
      }
    } else {
      respuesta = "ERROR: Formato incorrecto (n,m,p)";
    }

  } else if (comando == "DVL+QFIL") {
    respuesta =
        "FIL0=" + String(filterADC[0][0]) + "," + String(filterADC[0][1]) +
        " | FIL1=" + String(filterADC[1][0]) + "," + String(filterADC[1][1]);

  } else if (comando.startsWith("DVL+SFACTOR=")) {
    String args = comando.substring(12);
    int firstComma = args.indexOf(',');
    int secondComma = args.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      int n = args.substring(0, firstComma).toInt();
      float m = args.substring(firstComma + 1, secondComma).toFloat();
      float p = args.substring(secondComma + 1).toFloat();

      if (n >= 0 && n < 2) {
        paramADC[n][0] = m;
        paramADC[n][1] = p;

        preferences.begin("adc_config", false);
        String keyFactor = "param_" + String(n) + "_0";
        String keyOffset = "param_" + String(n) + "_1";
        preferences.putFloat(keyFactor.c_str(), m);
        preferences.putFloat(keyOffset.c_str(), p);
        preferences.end();

        respuesta = "FACTOR SETEADO OK";
      } else {
        respuesta = "ERROR: Indice fuera de rango (0-1)";
      }
    } else {
      respuesta = "ERROR: Formato incorrecto (n,m,p)";
    }

  } else if (comando == "DVL+QFACTOR") {
    respuesta =
        "FAC0=" + String(paramADC[0][0]) + "," + String(paramADC[0][1]) +
        " | FAC1=" + String(paramADC[1][0]) + "," + String(paramADC[1][1]);

  } else if (comando.startsWith("DVL+SALI=")) {
    int val = comando.substring(9).toInt();
    if (val > 0) {
      cantMed = val;
      preferences.begin("adc_config", false);
      preferences.putInt("cantMed", cantMed);
      preferences.end();
      respuesta = "ALISADO SETEADO OK";
    } else {
      respuesta = "ERROR: Valor debe ser mayor a 0";
    }

  } else if (comando == "DVL+QALI") {
    respuesta = "ALISADO=" + String(cantMed);

  } else if (comando.startsWith("DVL+STADC=")) {
    int val = comando.substring(10).toInt();
    if (val >= 1 && val <= 65000) {
      tADC = (uint16_t)val;
      preferences.begin("adc_config", false);
      preferences.putInt("tADC", tADC);
      preferences.end();
      respuesta = "TIME ADC SETEADO OK";
    } else {
      respuesta = "ERROR: Valor fuera de rango (1-65000)";
    }

  } else if (comando == "DVL+QTADC") {
    respuesta = "TIME ADC=" + String(tADC);

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
