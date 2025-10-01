#include "Comandos.h"
#include "Flash.h"
#include "GNSS.h" // Para setLatitude, setLongitude, setLocationValid
#include <Preferences.h>
#include "FOTA.h"
#include "DS18B20.h"


extern Preferences preferences;

String startMarker = "DATA,";
String endMarker = "\r";
extern String versionado;
extern String ident;
extern String topic1;
char separator = ',';
String sensorValues[16]; // S0...S15

extern HardwareSerial SensorSerial;

uint en_sensor;
uint en_serial;


extern unsigned long publishInterval;


extern FOTAClass FOTA;


String procesarComando(String comando) {
  comando.trim();
  String respuesta = "";
  

  /* COMANDO LATITUD */ 
  if (comando.startsWith("DVL+SLAT=")) {
    String nuevaLat = comando.substring(9);
    guardar_en_flash("lat", nuevaLat);
    ultimaLat = nuevaLat;
    setLatitude(nuevaLat);
    setLocationValid(true);
    respuesta = "RLAT_OK";

  /* COMANDO LONGITUD */ 
  } else if (comando.startsWith("DVL+SLONG=")) {
    String nuevaLon = comando.substring(10);
    guardar_en_flash("lon", nuevaLon);
    ultimaLon = nuevaLon;
    setLongitude(nuevaLon);
    setLocationValid(true);
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

  /* COMANDO ID */ 
  } else if (comando.startsWith("DVL+ID=")) {
    String ident_s = comando.substring(7);
    guardar_en_flash("ident", ident_s);
    ident = ident_s;

    topic1 = "DVL/LILY-GO/" + ident;
    respuesta = "Rident_OK (TOPIC: " + topic1 + ")";

  } else if (comando.startsWith("DVL+FOTA=")) {
    String urlFOTA = comando.substring(9);
    urlFOTA.trim();
    FOTA.startUpdate(urlFOTA);
    preferences.begin("fota", false);
    preferences.putString("url", urlFOTA);  // Guarda la nueva URL
    preferences.end();
    respuesta = "FOTA_INICIADA";

  } else if (comando == "DVL+VER") {
    respuesta = "VERSION=" + versionado;

/*comando para habilitar sensor*/
  } else if (comando.startsWith("DVL+EN_SENSOR")) {
    en_sensor = 1;
    preferences.begin("enables", false);
    preferences.putUInt("sensor", en_sensor);
    preferences.end();
    respuesta = ">> HABILITADO LECTURA SENSOR";

/*comando para habilitar puerto serial secundario*/
  } else if (comando.startsWith("DVL+EN_SERIAL")) {
    en_serial = 1;
    preferences.begin("enables", false);
    preferences.putUInt("serial", en_serial);
    preferences.end();
    respuesta = ">> HABILITADO LECTURA SERIAL";
  } else if (comando == "DVL+RESET") {
    respuesta = ">> Reiniciando dispositivo...";
    Serial.println(respuesta);  // Lo mostramos antes del reset
    delay(100);                 // Pequeña pausa para que se imprima correctamente
    ESP.restart();
  } else if (comando == "DVL+EXP_RESET") {
    respuesta = ">> Reiniciando expansora...";
    Serial.println(respuesta);
    digitalWrite(SENSOR_POWER_PIN, LOW);   // Apaga la expansora
    delay(1000);                            // Espera 1 segundo
    digitalWrite(SENSOR_POWER_PIN, HIGH);  // Vuelve a encenderla


/*COMANDOS DE CONSULTA*/
  } else if (comando == "DVL+QTIME") {
    respuesta = "TIME=" + String(publishInterval);

  } else if (comando == "DVL+QFINI") {
    respuesta = "START_MARKER=" + startMarker;

  } else if (comando == "DVL+QFFIN") {
    respuesta = "END_MARKER=" + endMarker;

  } else if (comando == "DVL+QPARSE") {
    respuesta = "SEPARATOR=" + String(separator);

  } else if (comando == "DVL+QLAT") {
    respuesta = "LAT=" + ultimaLat;

  } else if (comando == "DVL+QLONG") {
    respuesta = "LONG=" + ultimaLon;

  } else if (comando == "DVL+QEN_SENSOR") {
    preferences.begin("enables", true);
    en_sensor = preferences.getUInt("sensor", 0);
    preferences.end();
    respuesta = "EN_SENSOR=" + String(en_sensor);

  } else if (comando == "DVL+QEN_SERIAL") {
    preferences.begin("enables", true);
    en_serial = preferences.getUInt("serial", 0);
    preferences.end();
    respuesta = "EN_SERIAL=" + String(en_serial);
    
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
  }} else {
    respuesta = "[ERR] Comando no reconocido.";
  }

  // Siempre lo mostramos tambien por Serial
  Serial.println(respuesta);
  return respuesta;
}



void guardarConfiguracionParser() {
  preferences.begin("parser", false);    //Abre un espacio (namespace) llamado "parser" para escribir (false significa modo escritura).
  preferences.putString("start", startMarker);
  preferences.putString("end", endMarker);
  preferences.putUChar("sep", separator);
  preferences.end();
}

void cargarConfiguracionParser() {
  preferences.begin("parser", true);
  startMarker = preferences.getString("start", "DATA,"); //esto carga los datos escritos en memoria. El primer parametro es la 'clave' con el que se guarda esa variable en flash, y el segundo, se usa como valor si no hay nada guardado en la flash
  endMarker = preferences.getString("end", "\r");
  separator = preferences.getUChar("sep", ',');
  preferences.end();
}

String getStartMarker() {
  return startMarker;
}

String getEndMarker() {
  return endMarker;
}

char getSeparator() {
  return separator;
}

String* getSensorValues() {
  return sensorValues;
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


