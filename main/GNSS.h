#ifndef GNSS_H
#define GNSS_H

#define TINY_GSM_MODEM_SIM7000

#include <TinyGsmClient.h>

bool setupGPS(TinyGsm &modem);
String getFormattedDateTime();
void setLatitude(const String &lat);
void setLongitude(const String &lon);
void setLocationValid(bool valid);
String getLatitude();
String getLongitude();
bool isLocationValid();
void disableGPS(void);
void enableGPS(void);

#endif
