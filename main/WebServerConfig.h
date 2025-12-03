#ifndef WEBSERVERCONFIG_H
#define WEBSERVERCONFIG_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WebServer server;
extern WebServer adminServer;
extern String ssid;
extern String password;

void handleRoot();
void handleSave();
void handleSavePrivado();
void handleRootPrivado();
void iniciarModoConfiguracion();
void iniciarWebServerPrivado();

#endif
