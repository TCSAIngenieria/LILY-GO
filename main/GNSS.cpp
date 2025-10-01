#include "GNSS.h"
#include <Wire.h>
#include <time.h>

#define SerialAT Serial1

static String latitude = "";
static String longitude = "";
static bool locationValid = false;
static bool hasFix = false;
static bool gpsChecked = false;


extern TinyGsm modem;



/*FUNCION PARA ENCENDER EL GPS*/
void enableGPS(void)
{
    // Set Modem GPS Power Control Pin to HIGH ,turn on GPS power
    // Only in version 20200415 is there a function to control GPS power
    modem.sendAT("+CGPIO=0,48,1,1");
    if (modem.waitResponse(10000L) != 1) {
        DBG("Set GPS Power HIGH Failed");
    }
    modem.enableGPS();
}



void disableGPS(void)
{
    // Set Modem GPS Power Control Pin to LOW ,turn off GPS power
    // Only in version 20200415 is there a function to control GPS power
    modem.sendAT("+CGPIO=0,48,1,0");
    if (modem.waitResponse(10000L) != 1) {
        DBG("Set GPS Power LOW Failed");
    }
    modem.disableGPS();
}





void setLatitude(const String &lat) {
    latitude = lat;
}

void setLongitude(const String &lon) {
    longitude = lon;
}

void setLocationValid(bool valid) {
    locationValid = valid;
}

String getLatitude() {
    return latitude;
}

String getLongitude() {
    return longitude;
}

//Funcion para saber si es valida la posicion GNSS
bool isLocationValid() {
    return locationValid;
}
