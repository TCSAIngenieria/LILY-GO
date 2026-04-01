#include "Flash.h"
#include "Debug.h"
#include <Preferences.h>
#include <SPIFFS.h>

Preferences prefs;

// Configuracion de SPIFFS
#define FORMAT_SPIFFS_IF_FAILED true

// Variables globales
String ultimaLat = "-33.123456";
String ultimaLon = "-64.123456";
extern String ident;

String spublishInterval;
unsigned long publishInterval = 60;

/*MEMORIA CIRCULAR*/
static int writeIndex = 0;
static int readIndex = 0;
static char currentPacket[MAX_PACKET_SIZE];  // paquete leido temporalmente
static bool packetLoaded = false;

void lectura_flash() {
  
  DVL_PRINTLN("Leyendo datos almacenados en flash...");

  ultimaLat = leer_de_flash("lat");
  ultimaLon = leer_de_flash("lon");
  spublishInterval = leer_de_flash("time");
  if (spublishInterval != "N/A" && spublishInterval.length() > 0) {
    unsigned long temp = strtoul(spublishInterval.c_str(), NULL, 10);
    if (temp > 0) {
      publishInterval = temp;
    }
  }

  DVL_PRINT("ultima Latitud guardada: ");
  DVL_PRINTLN(ultimaLat);
  DVL_PRINT("ultima Longitud guardada: ");
  DVL_PRINTLN(ultimaLon);
  DVL_PRINT("ultima ultimo publish_time guardado: ");
  DVL_PRINTLN(spublishInterval);
  DVL_PRINT(publishInterval);
}

void guardar_en_flash(const String& clave, const String& valor) {
  if (prefs.begin("gps_data", false)) {
    prefs.putString(clave.c_str(), valor);
    prefs.end();
    DVL_PRINT("Guardado en flash → ");
    DVL_PRINT(clave);
    DVL_PRINT(": ");
    DVL_PRINTLN(valor);
  } else {
    DVL_PRINTLN("No se pudo abrir la NVS para escritura.");
  }
}

String leer_de_flash(const String& clave, const String& valorPorDefecto) {
  String resultado = valorPorDefecto;
  if (prefs.begin("gps_data", true)) {
    resultado = prefs.getString(clave.c_str(), valorPorDefecto);
    prefs.end();
  } else {
    DVL_PRINTLN("No se pudo abrir la NVS para lectura.");
  }
  return resultado;
}

void flash_init() {
  // Iniciar SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    DVL_PRINTLN("Error mount SPIFFS");
  }
  // Iniciar Preferences
  if (prefs.begin("flash_buf", true)) {  // lectura
    writeIndex = prefs.getInt("widx", 0);
    readIndex = prefs.getInt("ridx", 0);
    prefs.end();
  } else {
    DVL_PRINTLN("No pudo abrir Preferences");
  }
}

bool flash_buffer_full() {
  return ((writeIndex + 1) % MAX_PACKETS) == readIndex;
}

// Retorna true si el buffer esta vacio
bool flash_buffer_empty() {
  return writeIndex == readIndex;
}

bool flash_save_packet(const char* json) {
  if (flash_buffer_full()) {
    // Si esta lleno, adelanta readIndex para "pisar" el paquete mas viejo
    readIndex = (readIndex + 1) % MAX_PACKETS;
    DVL_PRINTLN(" Buffer lleno, se sobrescribira el paquete mas viejo");
  }

  // Guardar json en SPIFFS en archivo packet_writeIndex.json
  char filename[32];
  sprintf(filename, "/packet_%d.json", writeIndex);

  File f = SPIFFS.open(filename, FILE_WRITE);
  if (!f) {
    DVL_PRINTLN("Error al abrir archivo para guardar paquete");
    return false;
  }
  f.write((const uint8_t*)json, strlen(json));
  f.close();

  writeIndex = (writeIndex + 1) % MAX_PACKETS;

  // Guardar indices en Preferences
  if (prefs.begin("flash_buf", false)) {
    prefs.putInt("widx", writeIndex);
    prefs.putInt("ridx", readIndex);  // actualiza tambien el readIndex si se piso
    prefs.end();
  }

  DVL_PRINTF("Guardado paquete en flash index=%d\n", (writeIndex == 0) ? MAX_PACKETS - 1 : writeIndex - 1);
  return true;
}

const char* flash_get_next_packet() {
  if (flash_buffer_empty()) return nullptr;
  if (packetLoaded) return currentPacket;

  // Leer archivo readIndex
  char filename[32];
  sprintf(filename, "/packet_%d.json", readIndex);

  File f = SPIFFS.open(filename, FILE_READ);
  if (!f) {
    DVL_PRINTLN("Error abriendo archivo para leer paquete");
    return nullptr;
  }
  size_t len = f.size();
  if (len >= MAX_PACKET_SIZE) {
    DVL_PRINTLN("Paquete demasiado grande");
    f.close();
    return nullptr;
  }
  f.readBytes(currentPacket, len);
  currentPacket[len] = '\0';
  f.close();

  packetLoaded = true;
  return currentPacket;
}

void flash_mark_packet_sent() {
  if (flash_buffer_empty()) return;

  // Borrar archivo de paquete enviado
  char filename[32];
  sprintf(filename, "/packet_%d.json", readIndex);
  SPIFFS.remove(filename);

  readIndex = (readIndex + 1) % MAX_PACKETS;
  packetLoaded = false;

  // Guardar indice en Preferences
  if (prefs.begin("flash_buf", false)) {
    prefs.putInt("ridx", readIndex);
    prefs.end();
  }

  DVL_PRINTF("Paquete indice %d marcado como enviado\n", (readIndex == 0) ? MAX_PACKETS - 1 : readIndex - 1);
}
