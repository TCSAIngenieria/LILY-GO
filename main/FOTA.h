#ifndef FOTA_H
#define FOTA_H

#include <Arduino.h>

class FOTAClass {
public:
  // Inicia la actualizacion OTA con la URL
  void startUpdate(const String& url);
};

extern FOTAClass FOTA;
bool parsearURL(String url, String &protocol, String &host, int &port, String &path);

#endif
