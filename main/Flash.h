#ifndef FLASH_H
#define FLASH_H


#define MAX_PACKETS 1440
#define MAX_PACKET_SIZE 512


#include <Arduino.h>

void lectura_flash();  // carga las variables iniciales
void guardar_en_flash(const String& clave, const String& valor);
String leer_de_flash(const String& clave, const String& valorPorDefecto = "N/A");

void flash_init();

bool flash_buffer_full();
bool flash_buffer_empty();

// Variables accesibles desde el main
extern String ultimaLat;
extern String ultimaLon;

// Guarda un paquete JSON en el buffer flash (no bloqueante)
bool flash_save_packet(const char* json);

// Obtiene el siguiente paquete para enviar (sin borrar)
const char* flash_get_next_packet();

// Marca el paquete enviado para avanzar el puntero
void flash_mark_packet_sent();

#endif 
