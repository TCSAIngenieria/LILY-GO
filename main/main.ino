#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

#define SerialAT Serial1
#define SerialMon Serial

#include <SPI.h>
#include <TinyGsmClient.h>

// #include <SD.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ADC.h"
#include "Comandos.h"
#include "Debug.h"
#include "FOTA.h"
#include "Fechayhora.h"
#include "Flash.h"
#include "GNSS.h"
#include "GPRS.h"
#include "MQTT.h"
#include "TensionAlimentacion.h"

#include "WebServerConfig.h"

// Prototypes
unsigned long obtener_y_avanzar_numero_paquete();
void conectar_WiFi();

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(modemStream);
#endif

WiFiClient espClient;
TinyGsmClient gsmClient(modem);

// Cliente MQTT comun para los dos tipos de conectividad
PubSubClient mqtt(espClient); // lo inicializamos con uno cualquiera

#define LED_PIN 2
#define WDT_TIMEOUT 120 // segundos para que reinicie por watchdog

// Pines para lógica de Pivot y Sirena
#define PIN_IN_1 13
#define PIN_IN_2 14
#define PIN_SIREN 15

String versionado = "V03.01.01-AGD_Pivots";

/*VARIABLES MQTT*/
unsigned long ledTimer = 0;
bool ledState = false;
bool mqttActivo = false;
unsigned long mqttUltimaConexionOK = 0;
const unsigned long MQTT_TIMEOUT = 300000; // 5 minutos

unsigned long lastMqttResubscribe = 0;
const unsigned long mqttResubscribeInterval = 10000; // cada 10 segundos
unsigned long lastTimeSyncAttempt = 0;
const unsigned long timeSyncInterval =
    10000; // Intentar sincronizar cada 10 segundos al despertar
bool passthroughMode = false;

// Variables generales
String ident = "";
String topic1;
unsigned long numeroPaquete = 0;
unsigned long rebootCount = 0;

// Habilitacion modulos
extern uint en_sensor;
extern uint en_serial;
extern uint en_modbus;
extern uint en_ble;
extern uint en_adc;
extern uint en_modem;
extern uint en_gps;
extern uint en_wifi;
extern uint en_pivot; // Sistema Pivot/Alarma habilitado
extern unsigned long delay_sirena;
extern unsigned long siren_duration;
extern unsigned long deep_sleep_time;

// WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;

// Credenciales GPRS
extern String apn;
const char gprsUser[] = "";
const char gprsPass[] = "";

// Datos estaticos del modem
String modemIMEI = "";
String modemIMSI = "";
String modemICCID = "";

// conectividad
bool TINY_GSM_USE_GPRS = true;  // uso del gprs
bool TINY_GSM_USE_WIFI = false; // uso del wifi

// Control de fases
bool faseGPS = true;           // Empezamos buscando latitud/longitud e inicializando modem
bool faseGPRS_WIFI = false;    // Activamos GPRS/WiFi luego de la fase inicial
bool relojActualizado = false; // Indica si ya se actualizo el reloj desde NTP

/*Tiempo de busqueda de latitud y longitud de GPS al inicio*/
unsigned long tiempoInicioGPS = 0;
const unsigned long tiempoLimiteGPS = 1UL * 60UL * 1000UL; // 1 minuto (cuando en_gps == 1)

///*Tiempo de actualizacion de Hora*/
unsigned long ultimaActualizacionNTP = 0;
const unsigned long intervaloNTP = 8UL * 60UL * 60UL * 1000UL; // 8 horas

/* Temporizadores para intentos de GPRS y NTP */
unsigned long lastGprsConnectAttempt = 0;
const unsigned long gprsConnectInterval = 10000; // 10 segundos

unsigned long lastNtpSyncAttempt = 0;
const unsigned long ntpSyncInterval = 10000; // 10 segundos

/*Variables para reinicio de modem si no se inicia bien*/
int contadorErroresModem = 0;
const int limiteErroresModem = 5;

/* Posicion  */
extern String ultimaLat;
extern String ultimaLon;

/* Variable tiempo intento de coneccion wifi */
unsigned long startAttemptTime = millis();

/* Variables para envio de datos por mqtt */
bool firstPublish = true;
unsigned long lastPublish = 0;
extern unsigned long publishInterval; // intervalo de publicacion del dato
unsigned long lastReconnectAttempt = 0;
unsigned long lastNoDataMessage = 0;

/*BOToN MODO AP*/
#define AP_BUTTON_PIN 0        // GPIO del boton (ejemplo: GPIO 0)
#define BUTTON_PRESS_TIME 5000 // 5 segundos
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;

/* Sensor */
const unsigned long Sensor_read_Interval =
    5000; // intervalo de lectura de sensor //5 segundos por default

/*Lectura de paquetes en flash*/
const unsigned long Packets_read_Interval = 2000;
unsigned long last_flash_read = 0;

/*Variables para ADC*/
float ADCValue[2];
float filterADC[2][2] = {{0, 0}, {0, 0}};
float ADCValueAnt[2] = {0, 0};
int cantMed = 50;
float paramADC[2][2] = {{1, 0}, {1, 0}};
uint16_t tADC = 1;
uint16_t cADC = 0;

// Tarea paralela para entrar en modo AP
void buttonTaskTracker(void *pvParameters) {
  pinMode(AP_BUTTON_PIN, INPUT_PULLUP);
  unsigned long pressStart = 0;
  bool pressed = false;

  while (true) {
    if (digitalRead(AP_BUTTON_PIN) == LOW) {
      if (!pressed) {
        pressStart = millis();
        pressed = true;
      } else if (millis() - pressStart >= BUTTON_PRESS_TIME) {
        Serial.println("\n Boton presionado entrando en Tarea Paralela! "
                       "Reiniciando a Modo AP...");

        // Guardamos la bandera para forzar el AP en el reinicio
        Preferences pref_temp;
        pref_temp.begin("device", false);
        pref_temp.putBool("forceAP", true);
        pref_temp.end();
        ESP.restart();
      }
    } else {
      pressed = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  SerialMon.begin(115200); // puerto serial primario

  preferences.begin("enables", true);
  modemDebugLevel = (uint8_t)preferences.getUInt("modem_dbg", 0);
  modemTechMode = (uint8_t)preferences.getUInt("modem_tech", 0);
  en_sensor = 0; // Deshabilitado permanentemente para liberar IRAM
  en_serial = 0;
  en_modbus = 0;
  en_ble = 0;
  en_adc = preferences.getUInt("adc", 0);
  en_modem = preferences.getUInt("modem", 1);
  en_gps = preferences.getUInt("gps", 0);
  en_wifi = preferences.getUInt("wifi", 1);
  en_pivot = preferences.getUInt("pivot", 0);
  preferences.end();

  forceModemTechReconfig();
  iniciarSerialModem();

  // --- Contador de reinicios y Lectura de AP Mode ---
  preferences.begin("device", false);
  bool forceAP = preferences.getBool("forceAP", false);
  if (forceAP) {
    preferences.putBool("forceAP",
                        false); // Limpiar para que no re-entre siempre
  }
  rebootCount = preferences.getULong("reboot", 0);
  rebootCount++;
  preferences.putULong("reboot", rebootCount);
  delay_sirena = preferences.getULong("dsir", 300);
  siren_duration = preferences.getULong("tsir", 300);
  deep_sleep_time = preferences.getULong("ds_time", 300);
  preferences.end();

  /*CONFIGURACION WIFI*/
  preferences.begin("wifi", true);
  // ssid = preferences.getString("ssid", "Flash-PaPeR");
  // password = preferences.getString("password", "Ayanami84");
  ssid = preferences.getString("ssid", "Invitados");
  password = preferences.getString("password", "TCinvitados");
  preferences.end();
  if (forceAP) {
    Serial.println("\n===============================================");
    Serial.println(" Entrando en MODO CONFIGURACION (AP) INMEDIATO ");
    Serial.println("===============================================\n");

    pinMode(LED_PIN, OUTPUT);

    wifiConfigurado = false;
    iniciarModoConfiguracion();
    iniciarWebServerPrivado();

    // Bucle infinito, omite el modem y Sensores por completo
    while (true) {
      server.handleClient();
      adminServer.handleClient();

      // Parpadeo bien rapido (estroboscopico) de 50ms para modo AP
      if ((millis() / 50) % 2 == 0) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
      delay(10); // delay minimo para dar aire al perro guardian
    }
  }

  // Configuración de pines de Pivot/Sirena
  pinMode(PIN_IN_1, INPUT_PULLUP);
  pinMode(PIN_IN_2, INPUT_PULLUP);
  pinMode(PIN_SIREN, OUTPUT);
  digitalWrite(PIN_SIREN, LOW); // Sirena apagada al inicio

  // Lanza la tarea paralela de monitoreo del boton en el Nucleo 0
  xTaskCreatePinnedToCore(buttonTaskTracker, "ButtonTask", 4096, NULL, 1, NULL,
                          0);
  // ----------------------

  preferences.begin("adc_config", true);
  tADC = (uint16_t)preferences.getInt("tADC", 1);
  if (tADC == 0)
    tADC = 1; // Safety check
  preferences.end();

  if (en_modem) {
    asegurarModemEncendido(); // Verifica por AT si ya esta encendido antes de pulsar PWR_PIN
  } else {
    DVL_PRINTLN("[MODEM] Deshabilitado por configuracion (EN_MODEM=0).");
    TINY_GSM_USE_WIFI = true;
    TINY_GSM_USE_GPRS = false;
  }

  static const esp_task_wdt_config_t wdt_config = {
      .timeout_ms = WDT_TIMEOUT * 1000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // ambos cores
      .trigger_panic = true};

  // Inicializa el watchdog
  esp_task_wdt_delete(NULL); // Elimina la tarea actual del watchdog anterior
  esp_task_wdt_deinit();     // Desactiva completamente el WDT actual
  esp_task_wdt_init(&wdt_config); // Ahora si, lo inicializa con 120 segundos
  esp_task_wdt_add(NULL);         // Agrega la tarea actual al nuevo WDT

  flash_init();
  initADC();
  initTensionAlimentacion();

  // Seteo el LED output
  pinMode(LED_PIN, OUTPUT); // Seteo el LED OFF

  /*BOToN AP*/
  pinMode(AP_BUTTON_PIN,
          INPUT_PULLUP); // Boton con logica inversa (presionado = LOW)

  /*LECTURA ID*/
  ident = leer_de_flash("ident", "60000");

  topic1 = "DVL/LILY-GO/" + ident;
  DVL_PRINT("TOPIC MQTT: ");
  DVL_PRINTLN(topic1);

  /*CONFIGURACIoN WEB SERVER ADMIN*/
  preferences.begin("mqtt", true);
  MQTT_BROKER = preferences.getString(
      "ip", "iot.tcsa.com.ar"); // ← solo usa este si no hay guardado
  MQTT_PORT = preferences.getInt("port", 1883); // ← idem
  preferences.end();

  /*BROKER MQTT*/
  DVL_PRINT("Broker cargado: ");
  DVL_PRINT(MQTT_BROKER);
  DVL_PRINT("  Puerto: ");
  DVL_PRINTLN(MQTT_PORT);

  if (en_wifi == 1 && ssid.length() > 0) {
    conectar_WiFi();
  } else {
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
  }

  lectura_flash();

  /*Configuracion MQTT*/
  mqtt.setSocketTimeout(20); // Timeout de socket reducido a 20s para detectar caidas mas rapido
  mqtt.setKeepAlive(30);     // Ping cada 30 segundos para mantener vivo el NAT celular y detectar desconexiones
  mqtt.setBufferSize(1024);
  mqtt.setServer(MQTT_BROKER.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  preferences.begin("fota", true);
  String storedURL = preferences.getString("url", "");
  preferences.end();

  if (storedURL.length() == 0) {
    preferences.begin("fota", false);
    preferences.putString("url", "https://raw.githubusercontent.com/");
    preferences.end();
  }
}

void loop() {
  unsigned long now = millis();
  static unsigned long unahora = 0;
  static int activeMqttClient = 0; // 0=ninguno, 1=WiFi, 2=GPRS
  static unsigned long last_100ms_event = 0;

  if (passthroughMode) {
    DVL_PRINTLN("\n[PASSTHROUGH] Modo transparente ACTIVADO.");
    DVL_PRINTLN("[PASSTHROUGH] Envie comandos AT directamente al modem.");
    DVL_PRINTLN("[PASSTHROUGH] Para salir, envie: DVL+PASSOFF\n");

    // Deshabilitar el watchdog para que no reinicie durante el debug manual
    esp_task_wdt_delete(NULL);

    while (passthroughMode) {
      if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input == "DVL+PASSOFF") {
          DVL_PRINTLN("\n[PASSTHROUGH] Desactivando y reiniciando...");
          delay(500);
          ESP.restart();
        } else if (input.length() > 0) {
          SerialAT.println(input);
        }
      }
      if (SerialAT.available()) {
        Serial.write(SerialAT.read());
      }
    }
  }

  // Muestreo periodico de canales ADC cada 100ms * tADC
  if (now - last_100ms_event >= 100) {
    last_100ms_event = now;
    if (++cADC >= tADC) {
      procesarADC(ADCValue);
      cADC = 0;
    }
  }

  // ========== FASE GPS E INICIALIZACION SUPERVISADA DE MODEM ==========
  if (faseGPS) {
    if (!en_modem) {
      DVL_PRINTLN("[MODEM] Deshabilitado por configuracion (EN_MODEM=0). Omitiendo GPS e inicio GSM.");
      faseGPS = false;
      faseGPRS_WIFI = true;
      digitalWrite(LED_PIN, false);
    } else {
      if (en_gps && tiempoLimiteGPS > 0) {
        DVL_PRINTLN("[GPS] Entrando a fase GPS bloqueante...");
        enableGPS();
        float lat, lon;
        unsigned long tiempoInicioBloqueo = millis();
        bool fix_conseguido = false;

        while (!fix_conseguido &&
               millis() - tiempoInicioBloqueo < tiempoLimiteGPS) {

          esp_task_wdt_reset();
          if (modem.getGPS(&lat, &lon)) {
            fix_conseguido = true;
            String nuevaLat = String(lat, 6);
            String nuevaLon = String(lon, 6);
            setLocationValid(true);

            setLatitude(nuevaLat);
            setLongitude(nuevaLon);
            ultimaLat = nuevaLat;
            ultimaLon = nuevaLon;

            DVL_PRINTLN("GPS FIX conseguido:");
            DVL_PRINTLN("Latitud actual: " + ultimaLat);
            DVL_PRINTLN("Longitud actual: " + ultimaLon);

            guardar_en_flash("lat", nuevaLat);
            guardar_en_flash("lon", nuevaLon);
            esp_task_wdt_reset();
          }

          static unsigned long lastBlink = 0;
          if (millis() - lastBlink > 300) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            lastBlink = millis();
            esp_task_wdt_reset();
          }
          esp_task_wdt_reset();
          delay(10);
        }

        disableGPS();
        modemRestart();
      } else {
        DVL_PRINTLN("[GPS] Omitido (EN_GPS=0). Iniciando modem directamente para conexion celular...");
      }

      esp_task_wdt_reset();
      if (!modem.init()) {
        DVL_PRINTLN("[MODEM] Fallo en modem.init()");
        contadorErroresModem++;
        DVL_PRINT("[MODEM] Contador de errores de modem: ");
        DVL_PRINTLN(contadorErroresModem);

        if (contadorErroresModem >= limiteErroresModem) {
          DVL_PRINTLN("[MODEM] Se alcanzo el limite de errores. Reiniciando modem...");
          modemRestart();
          contadorErroresModem = 0;
          delay(3000);
          esp_task_wdt_reset();

          if (!modem.init()) {
            DVL_PRINTLN("[MODEM] Fallo tras reinicio forzado del modem. Reiniciando placa...");
            ESP.restart();
          } else {
            DVL_PRINTLN("[MODEM] Modem recuperado.");
            isModemReady = true;
            preconfigureModemAPN(modem, apn);
            faseGPS = false;
            faseGPRS_WIFI = true;
            digitalWrite(LED_PIN, false);
          }
        }
      } else {
        DVL_PRINTLN("[MODEM] Modem iniciado correctamente.");
        isModemReady = true;
        preconfigureModemAPN(modem, apn);
        faseGPS = false;
        faseGPRS_WIFI = true;
        digitalWrite(LED_PIN, false);
      }
    }
  }

  /* REPORTE DE ESTADO DE CONECTIVIDAD CADA 10 SEGUNDOS (USANDO ESTADO CACHEADO) */
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    if (TINY_GSM_USE_WIFI && WiFi.status() == WL_CONNECTED) {
      DVL_PRINTLN("Conectado por WiFi");
    } else if (TINY_GSM_USE_GPRS && isModemConnected && isGprsActive) {
      DVL_PRINTLN("Conectado por GPRS (" + getGSMTech() + ")");
    } else {
      DVL_PRINTLN("No hay conexion activa");
    }
    lastCheck = millis();
  }

  // ========== FASE GPRS/WIFI ==========
  if (faseGPRS_WIFI) {

    if (now - unahora >= (60UL * 1000UL * 60UL)) { // contador 1 hora
      unahora = now;
      if (en_wifi == 1 && WiFi.status() != WL_CONNECTED) {
        TINY_GSM_USE_WIFI = true;
        TINY_GSM_USE_GPRS = false;
      }
    }

    if (TINY_GSM_USE_WIFI == true && TINY_GSM_USE_GPRS == false &&
        WiFi.status() != WL_CONNECTED) {
      if (activeMqttClient != 1) {
        mqtt.disconnect();
        mqtt.setClient(espClient);
        activeMqttClient = 1;
      }
      conectar_WiFi();

    } else if (TINY_GSM_USE_WIFI == false && TINY_GSM_USE_GPRS == true && en_modem) {

      if (activeMqttClient != 2) {
        mqtt.disconnect();
        mqtt.setClient(gsmClient);
        activeMqttClient = 2;
      }
      updateNetworkConnection(modem); // Conexion y registro celular no bloqueante

      // Solo intentar activar contexto de datos GPRS cuando ya estamos registrados en una celda movil
      if (isModemConnected && !isGprsActive) {
        if (millis() - lastGprsConnectAttempt >= gprsConnectInterval) {
          lastGprsConnectAttempt = millis();
          static int gprsFailCount = 0;
          mqtt.disconnect(); // Asegurar que no quede estado MQTT previo abierto
          DVL_PRINTLN("[GPRS] Red movil registrada (" + getGSMTech() + "). Conectando sesion de datos con APN: " + apn + "...");
          esp_task_wdt_reset();
          if (modem.gprsConnect(apn.c_str(), gprsUser, gprsPass)) {
            esp_task_wdt_reset();
            isGprsActive = true;
            gprsFailCount = 0;
            lastReconnectAttempt = 0; // Conectar a MQTT inmediatamente
            DVL_PRINTLN("[GPRS] Sesion de datos conectada correctamente. IP: " + modem.localIP().toString());
            if (modemDebugLevel >= 1) {
              DVL_PRINTLN(getModemDetailedStatus(modem));
            }
            mqtt.setServer(MQTT_BROKER.c_str(), MQTT_PORT);
            mqtt.setCallback(mqttCallback);
          } else {
            esp_task_wdt_reset();
            isGprsActive = false;
            gprsFailCount++;
            DVL_PRINTF("[GPRS] Fallo al activar sesion de datos GPRS (intento %d/5)\n", gprsFailCount);
            if (modemDebugLevel >= 1) {
              DVL_PRINTLN(getModemDetailedStatus(modem));
            }
            if (gprsFailCount >= 5) {
              DVL_PRINTLN("[GPRS] 5 fallos consecutivos de sesion GPRS. Reiniciando modem para limpiar contexto PDP...");
              gprsFailCount = 0;
              modemRestart();
            }
          }
        }
      }

      if (isModemConnected && isGprsActive) {
        if (!relojActualizado) {
          if (millis() - lastNtpSyncAttempt >= ntpSyncInterval) {
            lastNtpSyncAttempt = millis();
            if (updateClockFromNTP(modem)) {
              relojActualizado = true;
              ultimaActualizacionNTP = millis();
            } else {
              DVL_PRINTLN("Fallo la actualizacion de NTP");
            }
          }
        } else if (millis() - ultimaActualizacionNTP >= intervaloNTP) {
          if (updateClockFromNTP(modem)) {
            ultimaActualizacionNTP = millis();
          }
        }
      }
    }
  }

  // 2. Captura de informacion estatica del modem (maximo cada 30s si esta vacia)
  if (en_modem && isModemReady && (modemIMEI.length() < 10 || modemICCID.length() < 10)) {
    static unsigned long lastModemQuery = 0;
    if (lastModemQuery == 0 || (millis() - lastModemQuery > 30000)) {
      modemIMEI = modem.getIMEI();
      modemIMSI = modem.getIMSI();
      modemICCID = modem.getSimCCID();
      modemICCID.toUpperCase();
      lastModemQuery = millis();
    }
  }

  // 3. Logica de reconexion MQTT usando estado de red cacheado
  bool hasNetwork = (TINY_GSM_USE_WIFI && WiFi.status() == WL_CONNECTED) ||
                    (TINY_GSM_USE_GPRS && en_modem && isModemConnected && isGprsActive);

  if (!hasNetwork && mqtt.connected()) {
    mqtt.disconnect();
  }

  if (hasNetwork && !mqtt.connected() &&
      (lastReconnectAttempt == 0 || (now - lastReconnectAttempt > 10000))) {
    lastReconnectAttempt = now;

    if (WiFi.status() == WL_CONNECTED && TINY_GSM_USE_WIFI) {
      if (activeMqttClient != 1) {
        mqtt.setClient(espClient);
        activeMqttClient = 1;
      }
      DVL_PRINTLN("Seleccionado cliente WiFi para MQTT");
    } else if (en_modem && isGprsActive) {
      if (activeMqttClient != 2) {
        mqtt.setClient(gsmClient);
        activeMqttClient = 2;
      }
      DVL_PRINTLN("Seleccionado cliente GSM para MQTT");
    }

    esp_task_wdt_reset();
    if (mqttConnect()) {
      lastReconnectAttempt = 0;
      contadorErroresModem = 0; // Resetear contador al conectar exitosamente
    } else {
      contadorErroresModem++;
      DVL_PRINTF("Fallo intento MQTT %d/%d\n", contadorErroresModem,
                 limiteErroresModem);
      if (contadorErroresModem >= limiteErroresModem) {
        DVL_PRINTLN(
            "!!! Limite de fallos MQTT alcanzado. Reiniciando Modem... !!!");
        modemRestart();
        contadorErroresModem = 0;
      }
    }
    esp_task_wdt_reset();
  }

  if (mqtt.connected()) {
    mqtt.loop();
  }

  // Flash reading logic (buffer)
  if (hasNetwork && mqtt.connected() && (now - last_flash_read > Packets_read_Interval)) {
    while (true) {
      const char *packet = flash_get_next_packet();
      esp_task_wdt_reset();
      if (packet == nullptr)
        break;
      last_flash_read = now;
      String packetStr = String(packet);
      String finalTopic = topic1;

      // Determinar el topic segun el contenido del JSON
      if (packetStr.indexOf("\"DI1\":") != -1) {
        finalTopic += "/PIVOT";
      } else if (packetStr.indexOf("\"adc0\":") != -1) {
        finalTopic += "/ADC";
      }

      if (publish_mqtt_json(finalTopic, packetStr)) {
        flash_mark_packet_sent();
        mqttUltimaConexionOK = millis();
      } else {
        break;
      }
    }
  }

  // ---- LOGICA DE ANTIRREBOTE (DEBOUNCE 3S) ----
  int currentIn1 = digitalRead(PIN_IN_1);
  int currentIn2 = digitalRead(PIN_IN_2);
  static int lastReadIn1 = currentIn1;
  static int lastReadIn2 = currentIn2;
  static unsigned long lastChangeTime = millis();
  static int stableIn1 = currentIn1;
  static int stableIn2 = currentIn2;
  static bool stateChanged = false;

  if (currentIn1 != lastReadIn1 || currentIn2 != lastReadIn2) {
    lastChangeTime = millis();
    lastReadIn1 = currentIn1;
    lastReadIn2 = currentIn2;
  }

  if ((millis() - lastChangeTime) > 3000) {
    if (currentIn1 != stableIn1 || currentIn2 != stableIn2) {
      stableIn1 = currentIn1;
      stableIn2 = currentIn2;
      stateChanged = true;
    }
  }

  // ---- LOGICA PIVOT (ALARMAS Y DEEP SLEEP) ----
  bool isAlarm = false;
  if (en_pivot) {
    isAlarm = (stableIn1 == HIGH && stableIn2 == HIGH);
  }

  static bool wasAlarm = false;
  static bool pivotSentAtLeastOnce = false;

  if (stateChanged) {
    stateChanged = false;
    DVL_PRINTLN(
        "Cambio de estado detectado y estable (3s). Enviando reporte...");
    if (mqtt.connected()) {
      unsigned long numPkt = obtener_y_avanzar_numero_paquete();
      String jsonPivot = create_mqtt_json_pivot(
          ident, printCurrentTime(), stableIn1, stableIn2,
          digitalRead(PIN_SIREN), (int)en_pivot, leer_tension_backup(),
          leer_tension_principal(), numPkt);
      if (publish_mqtt_json(topic1 + "/PIVOT", jsonPivot)) {
        mqttUltimaConexionOK = millis();
        pivotSentAtLeastOnce = true;
      }
    }
  }

  // Sirena: solo actuar si el sistema Pivot esta habilitado
  static unsigned long alarmStartTime = 0;
  static bool alarmLatched = false;
  static unsigned long sirenOnStartTime = 0;

  if (en_pivot) {
    if (isAlarm || alarmLatched) {
      if (alarmStartTime == 0) {
        alarmStartTime = millis();
        if (alarmStartTime == 0)
          alarmStartTime = 1; // Asegurar que no sea 0
        DVL_PRINTLN("--- ALARMA DETECTADA ---");
        DVL_PRINT("Retardo configurado (seg): ");
        DVL_PRINTLN(delay_sirena);
      }

      unsigned long elapsed = millis() - alarmStartTime;

      // Si ya está enclavada, o si pasó el tiempo de retardo
      if (alarmLatched || (elapsed >= (delay_sirena * 1000UL))) {
        if (!alarmLatched) {
          alarmLatched = true;
          sirenOnStartTime = millis();
          if (sirenOnStartTime == 0)
            sirenOnStartTime = 1;
          DVL_PRINTLN("--- SIRENA ACTIVADA (ENCLAVADA) ---");
        }

        // Verificar tiempo de duración (timeout)
        if (siren_duration > 0 &&
            (millis() - sirenOnStartTime >= siren_duration * 1000UL)) {
          digitalWrite(PIN_SIREN, LOW);

          // --- Auto-deshabilitar el sistema tras timeout de sirena ---
          DVL_PRINTLN("--- SIRENA TIMEOUT: DESHABILITANDO SISTEMA PIVOT ---");
          en_pivot = 0;
          alarmLatched = false;
          alarmStartTime = 0;
          sirenOnStartTime = 0;

          // Persistir en flash
          preferences.begin("enables", false);
          preferences.putUInt("pivot", en_pivot);
          preferences.end();

          // Enviar reporte informando el cambio de estado
          if (mqtt.connected()) {
            unsigned long numPkt = obtener_y_avanzar_numero_paquete();
            String jsonPivot = create_mqtt_json_pivot(
                ident, printCurrentTime(), stableIn1, stableIn2,
                digitalRead(PIN_SIREN), (int)en_pivot, leer_tension_backup(),
                leer_tension_principal(), numPkt);
            if (publish_mqtt_json(topic1 + "/PIVOT", jsonPivot)) {
              mqttUltimaConexionOK = millis();
            }
          }
        } else {
          digitalWrite(PIN_SIREN, HIGH);
        }
      } else {
        // En periodo de retardo
        digitalWrite(PIN_SIREN, LOW); // Asegurar apagada durante la espera
        static unsigned long lastLog = 0;
        if (millis() - lastLog > 5000) {
          DVL_PRINT("Esperando retardo sirena... faltan: ");
          DVL_PRINTLN((delay_sirena * 1000UL - elapsed) / 1000);
          lastLog = millis();
        }
      }
    } else {
      // No hay alarma y no está enclavada
      alarmStartTime = 0;
      digitalWrite(PIN_SIREN, LOW);
    }
  } else {
    // Sistema deshabilitado o PIVOFF enviado
    if (alarmStartTime != 0 || alarmLatched) {
      DVL_PRINTLN("--- ALARMA DESACTIVADA O SISTEMA DESHABILITADO ---");
    }
    alarmStartTime = 0;
    alarmLatched = false;
    digitalWrite(PIN_SIREN, LOW);
  }

  // ---- ENViO DATOS CADA INTERVALO ----
  if (firstPublish || now - lastPublish > publishInterval * 1000) {
    bool wasFirst = firstPublish;
    const unsigned long MAX_WAIT_MQTT_MS = 180000UL; // Hasta 3 min para registrar red y conectar MQTT

    // 1. Al despertar o iniciar, esperar a que MQTT conecte antes de emitir el primer reporte
    // (El RTC interno del ESP32 conserva la hora en Deep Sleep, por lo que isTimeSet() ya es true
    // al despertar; si no esperamos a mqtt.connected(), guardaría en flash y se dormiría sin transmitir).
    if (wasFirst && !mqtt.connected() && (en_modem == 1 || en_wifi == 1) &&
        (now < MAX_WAIT_MQTT_MS)) {
      static unsigned long lastWaitMqttLog = 0;
      if (now - lastWaitMqttLog >= 10000) {
        lastWaitMqttLog = now;
        DVL_PRINTLN("[Reporte] Esperando conexion MQTT para transmitir reporte inicial...");
      }
      goto skip_publish;
    }

    // 2. Sincronización de reloj si aún no tiene hora válida (arranque en frío)
    if (wasFirst && !isTimeSet()) {
      if (now - lastTimeSyncAttempt > timeSyncInterval) {
        lastTimeSyncAttempt = now;
        DVL_PRINTLN("[Hora] Reloj no sincronizado. Intentando sincronizar...");
        if (WiFi.status() == WL_CONNECTED) {
          updateClockFromNTP_wifi();
        } else if (en_modem && isGprsActive) {
          if (updateClockFromNTP(modem)) {
            relojActualizado = true;
            ultimaActualizacionNTP = millis();
          }
        }
      }

      // Si aun no hay hora valida y no se agoto la ventana de espera, posponer el reporte
      if (!isTimeSet() && (now < MAX_WAIT_MQTT_MS)) {
        static unsigned long lastWarn = 0;
        if (now - lastWarn > 5000) {
          DVL_PRINTLN("[Hora] Sin hora valida aun. Reporte pospuesto.");
          lastWarn = now;
        }
        goto skip_publish;
      }
      if (isTimeSet()) {
        DVL_PRINTLN("[Hora] Reloj sincronizado correctamente.");
      }
    }

    lastPublish = now;
    firstPublish = false;

    // Actualizacion periodica de NTP via WiFi (solo en ciclos posteriores)
    if (WiFi.status() == WL_CONNECTED && !wasFirst) {
      updateClockFromNTP_wifi();
    }

    if (en_adc == 1) {
      String jsonadc = create_mqtt_json_adc(
          ident, printCurrentTime(), ADCValue[0], ADCValue[1]);
      if (mqtt.connected()) {
        if (publish_mqtt_json(topic1 + "/ADC", jsonadc))
          mqttUltimaConexionOK = millis();
      } else {
        flash_save_packet(jsonadc.c_str());
      }
    }

    float vBackup = leer_tension_backup();
    float vPrincipal = leer_tension_principal();

    // Reporte Periodico del Pivot (o al despertar)
    unsigned long numPktPivot = obtener_y_avanzar_numero_paquete();
    String jsonPivot = create_mqtt_json_pivot(
        ident, printCurrentTime(), stableIn1, stableIn2, digitalRead(PIN_SIREN),
        (int)en_pivot, vBackup, vPrincipal, numPktPivot);
    if (mqtt.connected()) {
      if (publish_mqtt_json(topic1 + "/PIVOT", jsonPivot)) {
        mqttUltimaConexionOK = millis();
        pivotSentAtLeastOnce = true;
      }
    } else {
      if (flash_save_packet(jsonPivot.c_str())) {
        DVL_PRINTLN("Reporte Pivot guardado en flash (offline).");
        pivotSentAtLeastOnce =
            true; // Permitir Deep Sleep tras agotar espera de conexion
      }
    }

    // SI ES EL PRIMER ENVIO (AL DESPERTAR), TAMBIEN ENVIAMOS KEEP ALIVE
    if (wasFirst) {
      DVL_PRINTLN("Primer reporte tras despertar. Enviando Keep Alive...");
      String cType = "NO_CONECTADO";
      String cDetail = "N/A";
      String rsrq = "N/A", rsrp = "N/A", rssi = "N/A";

      if (WiFi.status() == WL_CONNECTED) {
        cType = "WIFI";
        cDetail = WiFi.SSID();
        rssi = String(WiFi.RSSI());
      } else if (en_modem && (isGprsActive || isModemConnected)) {
        cType = "GSM";
        cDetail = getGSMTech();
        if (mqtt.connected()) {
          getModemSignalInfo(modem, rsrq, rsrp, rssi);
        }
      }

      if (mqtt.connected()) {
        String jsonKeepAlive = create_mqtt_json_keepalive(
            ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
            rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
            rsrp, rssi, vBackup, vPrincipal);
        publish_mqtt_json(topic1, jsonKeepAlive);
        lastNoDataMessage = millis();
      }
    }

    esp_task_wdt_reset();

    if (deep_sleep_time > 0 && en_pivot && !isAlarm && !alarmLatched &&
        pivotSentAtLeastOnce && (millis() - lastChangeTime > 3000)) {
      DVL_PRINTLN("Preparando Deep Sleep...");
      esp_sleep_enable_timer_wakeup((uint64_t)deep_sleep_time * 1000000ULL);
      uint64_t wakeMask = 0;
      if (stableIn1 == LOW)
        wakeMask |= (1ULL << PIN_IN_1);
      if (stableIn2 == LOW)
        wakeMask |= (1ULL << PIN_IN_2);
      if (wakeMask != 0)
        esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);

      // --- Apagar TODOS los periféricos antes de dormir ---

      // 1. Desconectar MQTT limpiamente asegurando vaciado de paquetes en Flash y buffer TCP
      if (mqtt.connected()) {
        while (true) {
          const char *packet = flash_get_next_packet();
          esp_task_wdt_reset();
          if (packet == nullptr)
            break;
          String packetStr = String(packet);
          String finalTopic = topic1;
          if (packetStr.indexOf("\"DI1\":") != -1) {
            finalTopic += "/PIVOT";
          } else if (packetStr.indexOf("\"adc0\":") != -1) {
            finalTopic += "/ADC";
          }
          if (publish_mqtt_json(finalTopic, packetStr)) {
            flash_mark_packet_sent();
          } else {
            break;
          }
        }
        mqtt.loop();
        delay(500);
        mqtt.disconnect();
        DVL_PRINTLN("[DeepSleep] MQTT desconectado.");
      }

      // 2. Apagar WiFi
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      DVL_PRINTLN("[DeepSleep] WiFi apagado.");

      // 3. Apagar módem de forma segura (solo si estaba encendido)
      if (isModemOn()) {
        modemPowerOff();
        DVL_PRINTLN("[DeepSleep] Modem apagado.");
      } else {
        DVL_PRINTLN("[DeepSleep] Modem ya se encontraba apagado.");
      }

      // 4. Apagar LED
      digitalWrite(LED_PIN, LOW);

      // 5. Asegurar sirena apagada (por seguridad redundante)
      digitalWrite(PIN_SIREN, LOW);

      DVL_PRINTLN("[DeepSleep] Entrando en Deep Sleep...");
      delay(100); // Pausa para que se impriman los logs por Serial
      esp_deep_sleep_start();
    }
  }
skip_publish:; // Etiqueta de salto si aún espera conexión o sincronización de hora

  // Keep Alive periodico
  if (!firstPublish && (now - lastNoDataMessage > 60000)) {
    lastNoDataMessage = now;
    String cType = "NO_CONECTADO";
    String cDetail = "N/A";
    String rsrq = "N/A";
    String rsrp = "N/A";
    String rssi = "N/A";

    if (WiFi.status() == WL_CONNECTED) {
      cType = "WIFI";
      cDetail = WiFi.SSID();
      rssi = String(WiFi.RSSI());
    } else if (en_modem && (isGprsActive || isModemConnected)) {
      cType = "GSM";
      cDetail = getGSMTech();
      if (mqtt.connected()) {
        getModemSignalInfo(modem, rsrq, rsrp, rssi);
      }
    }

    if (mqtt.connected()) {
      String jsonKeepAlive = create_mqtt_json_keepalive(
          ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
          rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
          rsrp, rssi, leer_tension_backup(), leer_tension_principal());
      publish_mqtt_json(topic1, jsonKeepAlive);
    } else if (modemDebugLevel >= 1) {
      DVL_PRINTLN("[MQTT-DBG] Keep-Alive omitido: esperando conexion activa al broker MQTT...");
    }
  }

  if (mqttActivo && millis() - mqttUltimaConexionOK > MQTT_TIMEOUT) {
    DVL_PRINTLN("🕒 Tiempo sin reconexion MQTT superado. Reiniciando...");
    delay(1000);
    ESP.restart();
  }

  escucharComandos();
  actualizarLED();
  esp_task_wdt_reset();
}

void actualizarLED() {
  unsigned long now = millis();

  // 1. MQTT Activo y debidamente conectado
  if (mqttActivo && mqtt.connected() &&
      (now - mqttUltimaConexionOK <= MQTT_TIMEOUT)) {
    int seqMQTT = now % 5000; // Ciclo de 5 segundos

    if (WiFi.status() == WL_CONNECTED) {
      // WiFi: 1 parpadeo de apagado cada 5 segundos
      if (seqMQTT >= 0 && seqMQTT < 100) {
        digitalWrite(LED_PIN, LOW);
      } else {
        digitalWrite(LED_PIN, HIGH);
      }
    } else {
      // GPRS: 2 parpadeos de apagado cada 5 segundos
      if ((seqMQTT >= 0 && seqMQTT < 100) || (seqMQTT > 200 && seqMQTT < 300)) {
        digitalWrite(LED_PIN, LOW);
      } else {
        digitalWrite(LED_PIN, HIGH);
      }
    }
    return;
  }

  // Si no hay conexion a MQTT, dependemos de que red estemos usando.
  int seq = now % 2000; // Ciclo general de 2 segundos para las secuencias

  // 2. Conectado a WiFi -> Triple parpadeo veloz (3 destellos cortos cada 2
  // seg)
  if (WiFi.status() == WL_CONNECTED) {
    if ((seq >= 0 && seq < 100) || (seq > 200 && seq < 300) ||
        (seq > 400 && seq < 500)) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    return;
  }

  // 3. Conectado a GPRS -> Parpadeo singular y corto (1 latido cada 2 seg)
  if (en_modem && (isGprsActive || isModemConnected)) {
    if (seq >= 0 && seq < 200) { // 200ms prendido, 1800ms apagado
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    return;
  }

  // 4. Completamente Desconectado -> LED Apagado
  digitalWrite(LED_PIN, LOW);
}

unsigned long obtener_y_avanzar_numero_paquete() {
  unsigned long actual = numeroPaquete;
  numeroPaquete++;
  if (numeroPaquete >= 7000)
    numeroPaquete = 0;
  return actual;
}

void conectar_WiFi() {
  if (en_wifi != 1) {
    DVL_PRINTLN("WiFi no habilitado en modo Cliente (EN_WIFI != 1).");
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
    return;
  }

  DVL_PRINTLN("Intentando conectar a WiFi guardada...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    DVL_PRINT(".");
    esp_task_wdt_reset();
  }

  if (WiFi.status() == WL_CONNECTED) {
    DVL_PRINTLN("Conectado a WiFi!");
    wifiConfigurado = true;
    TINY_GSM_USE_WIFI = true;
    TINY_GSM_USE_GPRS = false;
  } else {
    DVL_PRINTLN("No se pudo conectar a WiFi. Esperando boton...");
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
  }
}
