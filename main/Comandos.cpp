#include "Comandos.h"
#include "Debug.h"
#include "DS18B20.h"
#include "FOTA.h"
#include "Flash.h"
#include "Modbus.h"
#include <Preferences.h>
#include "MQTT.h"

extern Preferences preferences;

String startMarker = "DATA,";
String endMarker = "\r";
extern String versionado;
extern String ident;
extern String topic1;
char separator = ',';
String sensorValues[16]; // S0...S15

extern HardwareSerial SensorSerial;
extern HardwareSerial SensorSerial;
extern float filterADC[3][2];
extern float paramADC[3][2];
extern int cantMed;
extern uint16_t tADC;

uint en_sensor;
uint en_serial;
uint en_modbus;
uint en_ble;
uint en_adc;
uint en_modem;
uint en_gps;
uint en_wifi = 1;

extern unsigned long publishInterval;
extern FOTAClass FOTA;
uint32_t serialBaud = 4800;

String procesarComando(String comando) {
  comando.trim();
  String respuesta = "";

  /* COMANDO LATITUD */
  if (comando.startsWith("DVL+SLAT=")) {
    String nuevaLat = comando.substring(9);
    guardar_en_flash("lat", nuevaLat);
    ultimaLat = nuevaLat;
    respuesta = "RLAT_OK";

    /* COMANDO FILTRO ADC */
  } else if (comando.startsWith("DVL+SFIL=")) {
    // Formato: DVL+SFIL=n,m,p
    String args = comando.substring(9);
    int firstComma = args.indexOf(',');
    int secondComma = args.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      int n = args.substring(0, firstComma).toInt();
      float m = args.substring(firstComma + 1, secondComma).toFloat();
      float p = args.substring(secondComma + 1).toFloat();

      if (n >= 0 && n < 3) {
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
        respuesta = "ERROR: Indice fuera de rango (0-2)";
      }
    } else {
      respuesta = "ERROR: Formato incorrecto (n,m,p)";
    }

    /* COMANDO FACTOR ADC */
  } else if (comando.startsWith("DVL+SFACTOR=")) {
    // Formato: DVL+SFACTOR=n,m,p
    String args = comando.substring(12);
    int firstComma = args.indexOf(',');
    int secondComma = args.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      int n = args.substring(0, firstComma).toInt();
      float m = args.substring(firstComma + 1, secondComma).toFloat();
      float p = args.substring(secondComma + 1).toFloat();

      if (n >= 0 && n < 3) {
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
        respuesta = "ERROR: Indice fuera de rango (0-2)";
      }
    } else {
      respuesta = "ERROR: Formato incorrecto (n,m,p)";
    }

    /* COMANDO LONGITUD */
  } else if (comando.startsWith("DVL+SLONG=")) {
    String nuevaLon = comando.substring(10);
    guardar_en_flash("lon", nuevaLon);
    ultimaLon = nuevaLon;
    respuesta = "RLONG_OK";

    /* COMANDOS TRAMA DATOS */
  } else if (comando.startsWith("DVL+SFINI=")) {
    startMarker = comando.substring(10);
    guardarConfiguracionParser();
    respuesta = "[OK] StartMarker: " + startMarker;

  } else if (comando.startsWith("DVL+SFFIN=")) {
    endMarker = comando.substring(10);
    guardarConfiguracionParser();
    respuesta = "[OK] EndMarker: " + endMarker;

  } else if (comando.startsWith("DVL+SPARSE=")) {
    separator = comando.substring(11)[0];
    guardarConfiguracionParser();
    respuesta = "[OK] Separador: " + String(separator);

    /* COMANDOS TIEMPO PUBLICACIoN DATOS */
  } else if (comando.startsWith("DVL+STIME=")) {
    String publishInterval_s = comando.substring(10);
    guardar_en_flash("time", publishInterval_s);
    publishInterval = strtoul(publishInterval_s.c_str(), NULL, 10);
    respuesta = "TIEMPO SETEADO OK";

    /* COMANDO APN */
  } else if (comando.startsWith("DVL+SAPN=")) {
    String nuevoAPN = comando.substring(9);
    nuevoAPN.trim();
    guardar_en_flash("apn", nuevoAPN);
    apn = nuevoAPN;
    respuesta = "APN SETEADO OK";

    /* COMANDO ID */
  } else if (comando.startsWith("DVL+ID=")) {
    String ident_s = comando.substring(7);
    guardar_en_flash("ident", ident_s);
    ident = ident_s;

    topic1 = "DVL/NODEMCU/" + ident;

    // Desconectar de MQTT para forzar una revinculacion en el main loop
    // con el nuevo ID y nuevas subscripciones a los topicos (COMANDOS, FOTA)
    if (mqtt.connected()) {
      mqtt.disconnect();
    }

    respuesta = "Rident_OK (TOPIC: " + topic1 + ")";

  } else if (comando.startsWith("DVL+FOTA=")) {
    String urlFOTA = comando.substring(9);
    urlFOTA.trim();
    FOTA.startUpdate(urlFOTA);
    preferences.begin("fota", false);
    preferences.putString("url", urlFOTA); // Guarda la nueva URL
    preferences.end();
    respuesta = "FOTA_INICIADA";

  } else if (comando == "DVL+VER") {
    respuesta = "VERSION=" + versionado;

    /*comando para habilitar o deshabilitar sensor*/
  } else if (comando.startsWith("DVL+EN_SENSOR=")) {
    String v = comando.substring(String("DVL+EN_SENSOR=").length());
    v.trim();
    en_sensor = (v == "1") ? 1 : 0;
    preferences.begin("enables", false);
    preferences.putUInt("sensor", en_sensor);
    preferences.end();
    if (en_sensor == 1) {
      respuesta = ">> HABILITADO LECTURA SENSOR";
    } else {
      respuesta = ">> DESHABILITADO LECTURA SENSOR";
    }

    /*comando para habilitar o deshabilitar puerto serial secundario*/
  } else if (comando.startsWith("DVL+EN_SERIAL=")) {
    String v = comando.substring(String("DVL+EN_SERIAL=").length());
    v.trim();
    int val = v.toInt();
    en_serial = (val == 1 || val == 2) ? val : 0;
    if (en_serial == 1) {
      pinMode(SENSOR_POWER_PIN, OUTPUT);
      digitalWrite(SENSOR_POWER_PIN, HIGH);
      en_modbus = 0;
      modbus_set_enabled(false);
      preferences.begin("enables", false);
      preferences.putUInt("serial", 1);
      preferences.putUInt("modbus", 0);
      preferences.end();
      respuesta = ">> HABILITADO LECTURA SERIAL (MODBUS=0)";
    } else if (en_serial == 2) {
      pinMode(SENSOR_POWER_PIN, OUTPUT);
      digitalWrite(SENSOR_POWER_PIN, HIGH);
      en_modbus = 0;
      modbus_set_enabled(false);
      preferences.begin("enables", false);
      preferences.putUInt("serial", 2);
      preferences.putUInt("modbus", 0);
      preferences.end();
      respuesta = ">> HABILITADO MODO SERIAL BRIDGE (JSONs por UART2 cuando WiFi caido) (MODBUS=0)";
    } else {
      preferences.begin("enables", false);
      preferences.putUInt("serial", 0);
      preferences.end();
      respuesta = ">> DESHABILITADO LECTURA SERIAL";
    }
  } /*comando para habilitar MODBUS (excluyente con EN_SERIAL)*/
  else if (comando.startsWith("DVL+EN_MODBUS=")) {
    String v = comando.substring(String("DVL+EN_MODBUS=").length());
    v.trim();
    en_modbus = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("modbus", en_modbus);
    if (en_modbus == 1) {
      en_serial = 0; // Exclusión
      preferences.putUInt("serial", 0);
      modbus_set_enabled(true);
      respuesta = ">> HABILITADO MODBUS (EN_SERIAL=0)";
    } else {
      modbus_set_enabled(false);
      respuesta = ">> DESHABILITADO MODBUS";
    }
    preferences.end();
  }

  /*comando para habilitar o deshabilitar BLE*/
  else if (comando.startsWith("DVL+EN_BLE=")) {
    String v = comando.substring(String("DVL+EN_BLE=").length());
    v.trim();
    en_ble = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("ble", en_ble);
    if (en_ble == 1) {
      en_sensor = 0;
      en_modbus = 0;
      if (en_serial != 2) en_serial = 0;  // preserva modo bridge
      preferences.putUInt("sensor", 0);
      preferences.putUInt("serial", en_serial);
      preferences.putUInt("modbus", 0);
      if (en_serial == 2) {
        respuesta = ">> HABILITADO BLE (SERIAL BRIDGE MANTENIDO)";
      } else {
        respuesta = ">> HABILITADO BLE";
      }
    } else {
      respuesta = ">> DESHABILITADO BLE";
    }
    preferences.end();
  }

  /*comando para habilitar o deshabilitar ADC*/
  else if (comando.startsWith("DVL+EN_ADC=")) {
    String v = comando.substring(String("DVL+EN_ADC=").length());
    v.trim();
    en_adc = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("adc", en_adc);
    preferences.end();
    if (en_adc == 1) {
      respuesta = ">> HABILITADO REPORTE ADC INDEPENDIENTE";
    } else {
      respuesta = ">> DESHABILITADO REPORTE ADC INDEPENDIENTE";
    }
  }

  /*comando para habilitar o deshabilitar MODEM*/
  else if (comando.startsWith("DVL+EN_MODEM=")) {
    respuesta = ">> ERROR: EL EQUIPO NO DISPONE DE MODEM";
  }

  /*comando para habilitar o deshabilitar GPS*/
  else if (comando.startsWith("DVL+EN_GPS=")) {
    respuesta = ">> ERROR: EL EQUIPO NO DISPONE DE GPS";
  }

  /*comando para habilitar o deshabilitar WiFi*/
  else if (comando.startsWith("DVL+EN_WIFI=")) {
    String v = comando.substring(String("DVL+EN_WIFI=").length());
    v.trim();
    en_wifi = (v == "1") ? 1 : 0;

    preferences.begin("enables", false);
    preferences.putUInt("wifi", en_wifi);
    preferences.end();
    if (en_wifi == 1) {
      respuesta = ">> HABILITADO WIFI";
    } else {
      respuesta = ">> DESHABILITADO WIFI (MODO BRIDGE)";
    }
  }

  /* comando para configurar tramas:
     DVL+MODBUS=idx,ID,FUNC,LONG
     idx: 1..5, ID/FUNC/LONG en decimal o 0xNN hex
  */
  else if (comando.startsWith("DVL+MODBUS=")) {
    String args = comando.substring(String("DVL+MODBUS=").length());
    args.trim();

    int c1 = args.indexOf(',');
    int c2 = (c1 >= 0) ? args.indexOf(',', c1 + 1) : -1;
    int c3 = (c2 >= 0) ? args.indexOf(',', c2 + 1) : -1;
    int c4 = (c3 >= 0) ? args.indexOf(',', c3 + 1) : -1;

    if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) {
      respuesta = "[ERR] Formato: DVL+MODBUS=idx,ID,FUNC,ADDR,QTY";
    } else {
      auto parseAny = [](const String &s) -> long {
        return strtol(s.c_str(), nullptr, 0);
      };

      uint8_t idx = (uint8_t)parseAny(args.substring(0, c1));
      uint8_t id = (uint8_t)parseAny(args.substring(c1 + 1, c2));
      uint8_t func = (uint8_t)parseAny(args.substring(c2 + 1, c3));
      uint16_t addr = (uint16_t)parseAny(args.substring(c3 + 1, c4));
      uint16_t qty = (uint16_t)parseAny(args.substring(c4 + 1));

      if (idx < 1 || idx > 5) {
        respuesta = "[ERR] idx debe ser 1..5";
      } else {
        bool ok = modbus_set_frame(idx, id, func, addr, qty);
        if (ok) {
          respuesta = "[OK] MODBUS#" + String(idx) + " ID=0x" +
                      String(id, HEX) + " FUNC=0x" + String(func, HEX) +
                      " ADDR=0x" + String(addr, HEX) + " QTY=0x" +
                      String(qty, HEX);
        } else {
          respuesta = "[ERR] No se pudo guardar la trama";
        }
      }
    }
  }

  /* limpiar tramas: DVL+MODBUSCLR=idx | DVL+MODBUSCLR=ALL */
  else if (comando.startsWith("DVL+MODBUSCLR=")) {
    String arg = comando.substring(String("DVL+MODBUSCLR=").length());
    arg.trim();
    bool ok = false;
    if (arg.equalsIgnoreCase("ALL")) {
      ok = modbus_clear_frame(0);
      respuesta = ok ? "[OK] Todas las tramas Modbus borradas"
                     : "[ERR] No se pudo borrar";
    } else {
      uint8_t idx = (uint8_t)strtol(arg.c_str(), nullptr, 0);
      ok = modbus_clear_frame(idx);
      respuesta = ok ? "[OK] Trama Modbus #" + String(idx) + " borrada"
                     : "[ERR] idx invalido";
    }
  }

  /* consultas */
  else if (comando == "DVL+QEN_MODBUS") {
    preferences.begin("enables", true);
    en_modbus = preferences.getUInt("modbus", 0);
    preferences.end();
    respuesta = "EN_MODBUS=" + String(en_modbus);
  } else if (comando == "DVL+QMODBUS") {
    // Volcamos a Serial y devolvemos un breve resumen
    modbus_print_frames();
    respuesta = "[OK] Ver detalle por Serial";
  } else if (comando == "DVL+QFIL") {
    respuesta =
        "FIL0=" + String(filterADC[0][0]) + "," + String(filterADC[0][1]) +
        " | FIL1=" + String(filterADC[1][0]) + "," + String(filterADC[1][1]) +
        " | FIL2=" + String(filterADC[2][0]) + "," + String(filterADC[2][1]);

  } else if (comando == "DVL+QFACTOR") {
    respuesta =
        "FAC0=" + String(paramADC[0][0]) + "," + String(paramADC[0][1]) +
        " | FAC1=" + String(paramADC[1][0]) + "," + String(paramADC[1][1]) +
        " | FAC2=" + String(paramADC[2][0]) + "," + String(paramADC[2][1]);

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

    /* COMANDO TIME ADC */
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

  } else if (comando.startsWith("DVL+SBAUD=")) {
    unsigned long val = strtoul(comando.substring(10).c_str(), NULL, 10);
    if (val == 1200 || val == 2400 || val == 4800 || val == 9600 ||
        val == 19200 || val == 38400 || val == 57600 || val == 115200) {
      serialBaud = val;
      preferences.begin("serial_cfg", false);
      preferences.putULong("baud", serialBaud);
      preferences.end();
      SensorSerial.begin(serialBaud, SERIAL_8N1, 32, 33);
      respuesta = ">> BAUD RATE SETEADO A " + String(serialBaud);
    } else {
      respuesta = "ERROR: Baud rate no soportado (1200,2400,4800,9600,19200,38400,57600,115200)";
    }

  } else if (comando == "DVL+QBAUD") {
    respuesta = "BAUD=" + String(serialBaud);

  } else if (comando == "DVL+QTADC") {
    respuesta = "TIME ADC=" + String(tADC);

  } else if (comando == "DVL+QALI") {
    respuesta = "ALISADO=" + String(cantMed);

  } else if (comando == "DVL+RESET") {
    respuesta = ">> Reiniciando dispositivo...";
    DVL_PRINTLN(respuesta); // Lo mostramos antes del reset
    delay(100); // Pequeña pausa para que se imprima correctamente
    ESP.restart();
  } else if (comando == "DVL+EXP_RESET") {
    respuesta = ">> Reiniciando expansora...";
    Serial.println(respuesta);
    digitalWrite(SENSOR_POWER_PIN, LOW);  // Apaga la expansora
    delay(1000);                          // Espera 1 segundo
    digitalWrite(SENSOR_POWER_PIN, HIGH); // Vuelve a encenderla

    /*COMANDOS DE CONSULTA*/
  } else if (comando == "DVL+QTIME") {
    respuesta = "TIME=" + String(publishInterval);

  } else if (comando == "DVL+QFINI") {
    respuesta = "START_MARKER=" + startMarker;

  } else if (comando == "DVL+QFFIN") {
    respuesta = "END_MARKER=" + endMarker;

  } else if (comando == "DVL+QPARSE") {
    respuesta = "SEPARATOR=" + String(separator);

  } else if (comando == "DVL+QAPN") {
    respuesta = "APN=" + apn;

  } else if (comando == "DVL+QLAT") {
    respuesta = "LAT=" + ultimaLat;

  } else if (comando == "DVL+QLONG") {
    respuesta = "LONG=" + ultimaLon;

  } else if (comando == "DVL+QEN_SENSOR") {
    preferences.begin("enables", true);
    en_sensor = preferences.getUInt("sensor", 0);
    preferences.end();
    respuesta = "EN_SENSOR=" + String(en_sensor);

  } else if (comando == "DVL+QFIL") {
    respuesta =
        "FIL0=" + String(filterADC[0][0]) + "," + String(filterADC[0][1]) +
        " | FIL1=" + String(filterADC[1][0]) + "," + String(filterADC[1][1]) +
        " | FIL2=" + String(filterADC[2][0]) + "," + String(filterADC[2][1]);

  } else if (comando == "DVL+QEN_SERIAL") {
    preferences.begin("enables", true);
    en_serial = preferences.getUInt("serial", 0);
    preferences.end();
    respuesta = "EN_SERIAL=" + String(en_serial);

  } else if (comando == "DVL+QEN_BLE") {
    preferences.begin("enables", true);
    en_ble = preferences.getUInt("ble", 0);
    preferences.end();
    respuesta = "EN_BLE=" + String(en_ble);

  } else if (comando == "DVL+QEN_ADC") {
    preferences.begin("enables", true);
    en_adc = preferences.getUInt("adc", 0);
    preferences.end();
    respuesta = "EN_ADC=" + String(en_adc);

  } else if (comando == "DVL+QEN_WIFI") {
    preferences.begin("enables", true);
    en_wifi = preferences.getUInt("wifi", 1);
    preferences.end();
    respuesta = "EN_WIFI=" + String(en_wifi);

  } else if (comando == "DVL+QEN_MODEM") {
    preferences.begin("enables", true);
    en_modem = preferences.getUInt("modem", 1);
    preferences.end();
    respuesta = "EN_MODEM=" + String(en_modem);

  } else if (comando == "DVL+QEN_GPS") {
    preferences.begin("enables", true);
    en_gps = preferences.getUInt("gps", 1);
    preferences.end();
    respuesta = "EN_GPS=" + String(en_gps);

  } else if (comando.startsWith("EXP+")) {
    // Reenvia el comando al puerto serial secundario
    SensorSerial.println(comando);

    // Esperar y leer respuesta de la expansora
    unsigned long startTime = millis();
    String respuestaExp = "";

    while (millis() - startTime < 5000) { // espera hasta 500 ms
      while (SensorSerial.available()) {
        char c = SensorSerial.read();
        respuestaExp += c;
      }
    }

    if (respuestaExp.length() > 0) {
      respuesta = "RESP_EXPANSORA: " + respuestaExp;
    } else {
      respuesta = "[WARN] No se recibio respuesta de la expansora.";
    }
  } else {
    respuesta = "[ERR] Comando no reconocido.";
  }

  // Siempre lo mostramos tambien por Serial
  Serial.println(respuesta);
  return respuesta;
}

void guardarConfiguracionParser() {
  preferences.begin("parser",
                    false); // Abre un espacio (namespace) llamado "parser" para
                            // escribir (false significa modo escritura).
  preferences.putString("start", startMarker);
  preferences.putString("end", endMarker);
  preferences.putUChar("sep", separator);
  preferences.end();
}

void cargarConfiguracionParser() {
  preferences.begin("parser", true);
  startMarker = preferences.getString(
      "start", "DATA,"); // esto carga los datos escritos en memoria. El primer
                         // parametro es la 'clave' con el que se guarda esa
                         // variable en flash, y el segundo, se usa como valor
                         // si no hay nada guardado en la flash
  endMarker = preferences.getString("end", "\r");
  separator = preferences.getUChar("sep", ',');
  preferences.end();
}

String getStartMarker() { return startMarker; }

String getEndMarker() { return endMarker; }

char getSeparator() { return separator; }

String *getSensorValues() { return sensorValues; }

void escucharComandos() {
  while (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();
    if (comando.length() > 0) {
      procesarComando(comando);
    }
  }
}
