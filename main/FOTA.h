#ifndef FOTA_H
#define FOTA_H

#include <Arduino.h>

class FOTAClass {
public:
    // Inicia la actualización OTA con la URL
    void startUpdate(const String& url);
};

extern FOTAClass FOTA;



#endif
