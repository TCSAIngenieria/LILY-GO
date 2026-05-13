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

#include "WebServerConfig.h"

// Prototypes
unsigned long obtener_y_avanzar_numero_paquete();
void conectar_WiFi();

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
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

String versionado = "V02.05.01-AGD_Pivots";

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
    30000; // Intentar sincronizar cada 30 segundos
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
extern uint en_pivot; // Sistema Pivot/Alarma habilitado
extern unsigned long delay_sirena;
extern unsigned long siren_duration;
extern unsigned long deep_sleep_time;

// WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;

// Valores puerto serial secundario;

// Credenciales GPRS
const char apn[] = "igprs.claro.com.ar"; // APN
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
bool faseGPS = true;           // Empezamos buscando latitud/longitud
bool faseGPRS_WIFI = false;    // Activamos GPRS luego del GPS
bool relojActualizado = false; // Indica si ya se actualizo el reloj desde NTP

/*Tiempo de busqueda de latitud y longitud de GPS al inicio*/
unsigned long tiempoInicioGPS = 0;
const unsigned long tiempoLimiteGPS = 0UL * 60UL * 1000UL; // 1 minutos

///*Tiempo de actualizacion de Hora*/
unsigned long ultimaActualizacionNTP = 0;
const unsigned long intervaloNTP = 8UL * 60UL * 60UL * 1000UL; // 8 horas

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
extern unsigned long publishInterval; // intervalo de publicacion del dato //60
                                      // segundos por default//
unsigned long lastReconnectAttempt = 0;
unsigned long lastNoDataMessage = 0;

/*BOToN MODO AP*/
#define AP_BUTTON_PIN 0        // GPIO del boton (ejemplo: GPIO 0)
#define BUTTON_PRESS_TIME 5000 // 5 segundos
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;

/* Sensor */
// SensorInterface *sensor;
// BLEMokoScanner bleScanner;

const unsigned long Sensor_read_Interval =
    5000; // intervalo de lectura de sensor //5 segundos por default

/*Lectura de paquetes en flash*/
const unsigned long Packets_read_Interval = 2000;
unsigned long last_flash_read = 0;

/*Variables para ADC*/
float ADCValue[3];
float filterADC[3][2] = {{0, 0}, {0, 0}, {0, 0}};
float ADCValueAnt[3] = {0, 0, 0};
int cantMed = 50;
float paramADC[3][2] = {{1, 0}, {1, 0}, {1, 0}};
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
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
  // PUERTO SERIAL EXTERNO   RX=GPIO32, TX=GPIO33

  // modbus_begin(&SensorSerial);
  // modbus_load_from_prefs();

  preferences.begin("enables", true);
  en_sensor = 0; // Deshabilitado permanentemente para liberar IRAM
  en_serial = 0;
  en_modbus = 0;
  en_ble = 0;
  en_adc = preferences.getUInt("adc", 0);
  en_pivot = preferences.getUInt("pivot", 0);
  preferences.end();

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

  /*ACA TENGO QUE PONER EL SENSOR QUE VOY A UTILIZAR*/

  modemPowerOn(); // Enciendo el modem celular

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

  // Inicializacion del sensor

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

  // La carga del WiFi se movio al principio del setup para verificar el modo AP

  if (ssid.length() > 0) {
    conectar_WiFi();
  }

  lectura_flash();

  /*Configuracion MQTT*/
  mqtt.setSocketTimeout(60); // Espera hasta 60 segundos para conectarse
  mqtt.setKeepAlive(60); // Envia un ping cada 60 segundos si no hay actividad
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

  if (passthroughMode) {
    DVL_PRINTLN("\n[PASSTHROUGH] Modo transparente ACTIVADO.");
    DVL_PRINTLN("[PASSTHROUGH] Envie comandos AT directamente al modem.");
    DVL_PRINTLN("[PASSTHROUGH] Para salir, envie: DVL+PASSOFF\n");

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
      esp_task_wdt_reset();
    }
  }

  // 1. Manejo de conexin fsica (WiFi o GSM)
  bool conexionFisicaOK = false;
  if (WiFi.status() == WL_CONNECTED) {
    conexionFisicaOK = true;
  } else {
    // Si no hay WiFi, intentamos GSM/GPRS
    updateNetworkConnection(modem);
    if (modem.isNetworkConnected() && !modem.isGprsConnected()) {
      static unsigned long lastGprsAttempt = 0;
      if (now - lastGprsAttempt > 10000) {
        lastGprsAttempt = now;
        modem.gprsConnect(apn, gprsUser, gprsPass);
      }
    }
    if (modem.isGprsConnected()) {
      conexionFisicaOK = true;
    }
  }
  faseGPRS_WIFI = conexionFisicaOK;

  // 2. Captura de informacin del modem (una sola vez)
  if (modemIMEI == "" && modem.getSimStatus() == 1) {
    modemIMEI = modem.getIMEI();
    modemIMSI = modem.getIMSI();
    modemICCID = modem.getSimCCID();
    modemICCID.toUpperCase();
  }

  // 3. L�gica de reconexi�n MQTT
  if (faseGPRS_WIFI && !mqtt.connected() &&
      (now - lastReconnectAttempt > 10000)) {
    lastReconnectAttempt = now;

    // Seleccionar el cliente correcto seg�n la conexi�n activa
    if (WiFi.status() == WL_CONNECTED) {
      mqtt.setClient(espClient);
      DVL_PRINTLN("Seleccionado cliente WiFi para MQTT");
    } else if (modem.isGprsConnected()) {
      mqtt.setClient(gsmClient);
      DVL_PRINTLN("Seleccionado cliente GSM para MQTT");
    }

    lastReconnectAttempt = now;
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
  }

  if (mqtt.connected()) {
    mqtt.loop();
  }

  // Flash reading logic (buffer)
  if (mqtt.connected() && (now - last_flash_read > Packets_read_Interval)) {
    while (true) {
      const char *packet = flash_get_next_packet();
      esp_task_wdt_reset();
      if (packet == nullptr)
        break;
      last_flash_read = now;
      if (publish_mqtt_json(topic1, String(packet))) {
        flash_mark_packet_sent();
        mqttUltimaConexionOK = millis();
      } else {
        break;
      }
    }
  }

  // ---- L�GICA DE ANTIRREBOTE (DEBOUNCE 3S) ----
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

  // ---- LGICA PIVOT (ALARMAS Y DEEP SLEEP) ----
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
          digitalRead(PIN_SIREN), (int)en_pivot, numPkt);
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

    // --- SINCRONIZACION DE RELOJ AL DESPERTAR DEL DEEP SLEEP ---
    // Antes del primer reporte, asegurar que el reloj este sincronizado.
    // El ESP32 sale del Deep Sleep con time() = 0, por lo que debemos
    // obtener la hora via NTP (WiFi o GSM) antes de generar timestamps.
    if (wasFirst && !isTimeSet()) {
      if (now - lastTimeSyncAttempt > timeSyncInterval) {
        lastTimeSyncAttempt = now;
        DVL_PRINTLN("[Hora] Reloj no sincronizado. Intentando sincronizar...");
        if (WiFi.status() == WL_CONNECTED) {
          updateClockFromNTP_wifi();
        } else if (modem.isGprsConnected()) {
          updateClockFromNTP(modem);
        }
      }

      // Si aun no hay hora valida, posponer el reporte hasta el proximo ciclo
      if (!isTimeSet()) {
        static unsigned long lastWarn = 0;
        if (now - lastWarn > 5000) {
          DVL_PRINTLN("[Hora] Sin hora valida aun. Reporte pospuesto.");
          lastWarn = now;
        }
        goto skip_publish;
      }
      DVL_PRINTLN("[Hora] Reloj sincronizado correctamente.");
    }

    lastPublish = now;
    firstPublish = false;

    // Actualizacion periodica de NTP via WiFi (solo en ciclos posteriores)
    if (WiFi.status() == WL_CONNECTED && !wasFirst) {
      updateClockFromNTP_wifi();
    }

    if (en_adc == 1) {
      String jsonadc = create_mqtt_json_adc(
          ident, printCurrentTime(), ADCValue[0], ADCValue[1], ADCValue[2]);
      if (mqtt.connected()) {
        if (publish_mqtt_json(topic1 + "/ADC", jsonadc))
          mqttUltimaConexionOK = millis();
      } else {
        flash_save_packet(jsonadc.c_str());
      }
    }

    // Reporte Peridico del Pivot (o al despertar)
    unsigned long numPktPivot = obtener_y_avanzar_numero_paquete();
    String jsonPivot = create_mqtt_json_pivot(
        ident, printCurrentTime(), stableIn1, stableIn2, digitalRead(PIN_SIREN),
        (int)en_pivot, numPktPivot);
    if (mqtt.connected()) {
      if (publish_mqtt_json(topic1 + "/PIVOT", jsonPivot)) {
        mqttUltimaConexionOK = millis();
        pivotSentAtLeastOnce = true;
      }
    } else {
      if (flash_save_packet(jsonPivot.c_str())) {
        DVL_PRINTLN("Reporte Pivot guardado en flash (offline).");
        pivotSentAtLeastOnce =
            true; // Permitir Deep Sleep aunque estemos offline
      }
    }

    // SI ES EL PRIMER ENVO (AL DESPERTAR), TAMBIN ENVIAMOS KEEP ALIVE
    if (wasFirst) {
      DVL_PRINTLN("Primer reporte tras despertar. Enviando Keep Alive...");
      String cType = "NO_CONECTADO";
      String cDetail = "N/A";
      String rsrq = "N/A", rsrp = "N/A", rssi = "N/A";

      if (WiFi.status() == WL_CONNECTED) {
        cType = "WIFI";
        cDetail = WiFi.SSID();
        rssi = String(WiFi.RSSI());
      } else if (modem.isGprsConnected()) {
        cType = "GSM";
        cDetail = getGSMTech();
        getModemSignalInfo(modem, rsrq, rsrp, rssi);
      }

      String jsonKeepAlive = create_mqtt_json_keepalive(
          ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
          rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
          rsrp, rssi);
      if (mqtt.connected()) {
        publish_mqtt_json(topic1, jsonKeepAlive);
        lastNoDataMessage = millis();
      }
    }

    esp_task_wdt_reset();

    if (deep_sleep_time > 0 && en_pivot && !isAlarm && !alarmLatched &&
        pivotSentAtLeastOnce && (millis() - lastChangeTime > 3000)) {
      DVL_PRINTLN("Deep Sleep...");
      esp_sleep_enable_timer_wakeup((uint64_t)deep_sleep_time * 1000000ULL);
      uint64_t wakeMask = 0;
      if (stableIn1 == LOW)
        wakeMask |= (1ULL << PIN_IN_1);
      if (stableIn2 == LOW)
        wakeMask |= (1ULL << PIN_IN_2);
      if (wakeMask != 0)
        esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);
      if (TINY_GSM_USE_GPRS)
        modemPowerOff();
      esp_deep_sleep_start();
    }
  }
skip_publish:; // Etiqueta de salto si el reloj no esta sincronizado

  // Keep Alive
  if (now - lastNoDataMessage > 60000) {
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
    } else if (modem.isGprsConnected()) {
      cType = "GSM";
      cDetail = getGSMTech();
      getModemSignalInfo(modem, rsrq, rsrp, rssi);
    }

    String jsonKeepAlive = create_mqtt_json_keepalive(
        ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
        rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
        rsrp, rssi);

    if (mqtt.connected()) {
      publish_mqtt_json(topic1, jsonKeepAlive);
    }
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
  if (modem.isGprsConnected() || modem.isNetworkConnected()) {
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
