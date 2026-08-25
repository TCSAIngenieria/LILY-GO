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
#include <ArduinoJson.h>

#include "ADC.h"
#include "BLE_MOKO.h"
#include "Comandos.h"
#include "DS18B20.h"
#include "Debug.h"
#include "FOTA.h"
#include "Fechayhora.h"
#include "Flash.h"
#include "GNSS.h"
#include "GPRS.h"
#include "MQTT.h"
#include "Modbus.h"
#include "SerialSecundario.h"
#include "TensionAlimentacion.h"
#include "WebServerConfig.h"
#include "BridgeAP.h"

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

HardwareSerial SensorSerial(2); // UART2

#define LED_PIN 2
#define WDT_TIMEOUT 120 // segundos para que reinicie por watchdog

String versionado = "V07.16.05";

/*VARIABLES MQTT*/
unsigned long ledTimer = 0;
bool ledState = false;
bool mqttActivo = false;
unsigned long mqttUltimaConexionOK = 0;
const unsigned long MQTT_TIMEOUT = 300000; // 5 minutos

unsigned long lastMqttResubscribe = 0;
const unsigned long mqttResubscribeInterval = 10000; // cada 10 segundos

// Variables generales
String ident = "";
String topic1;
unsigned long numeroPaquete = 0;
unsigned long rebootCount = 0;

// Habilitacion modulos
extern uint en_sensor;
extern uint en_serial;
extern uint en_modbus;
extern uint32_t serialBaud;
extern uint en_ble;
extern uint en_adc;
extern uint en_modem;
extern uint en_gps;
extern uint en_wifi;

// WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;

// Valores puerto serial secundario;
extern String sensorValues[16];

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
bool faseGPS = true;           // Empezamos buscando latitud/longitud
bool faseGPRS_WIFI = false;    // Activamos GPRS luego del GPS
bool relojActualizado = false; // Indica si ya se actualizo el reloj desde NTP

/*Tiempo de busqueda de latitud y longitud de GPS al inicio*/
unsigned long tiempoInicioGPS = 0;
const unsigned long tiempoLimiteGPS = 0UL * 60UL * 1000UL; // 1 minutos

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
unsigned long lastPublish = 0;
extern unsigned long publishInterval; // intervalo de publicacion del dato //60
                                      // segundos por default//
unsigned long lastReconnectAttempt = 0;

/*BOToN MODO AP*/
#define AP_BUTTON_PIN 0        // GPIO del boton (ejemplo: GPIO 0)
#define BUTTON_PRESS_TIME 5000 // 5 segundos
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;

/* Sensor */
SensorInterface *sensor;
BLEMokoScanner bleScanner;

unsigned long last_Sensor_read = 0;
const unsigned long Sensor_read_Interval =
    5000; // intervalo de lectura de sensor //5 segundos por default
String valorStr = "";

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
  iniciarSerialModem();

  preferences.begin("serial_cfg", true);
  serialBaud = preferences.getULong("baud", 4800);
  preferences.end();

  SensorSerial.setRxBufferSize(2048);
  SensorSerial.begin(serialBaud, SERIAL_8N1, 32,
                     33); // PUERTO SERIAL EXTERNO   RX=GPIO32, TX=GPIO33

  // Inicializar Modbus sobre el puerto secundario
  modbus_begin(&SensorSerial);
  modbus_load_from_prefs();

  preferences.begin("enables", true);
  en_sensor = preferences.getUInt("sensor", 0);
  en_serial = preferences.getUInt("serial", 0);
  en_modbus = preferences.getUInt("modbus", 0);
  en_ble = preferences.getUInt("ble", 0);
  en_adc = preferences.getUInt("adc", 0);
  en_modem = preferences.getUInt("modem", 1);
  en_gps = preferences.getUInt("gps", 1);
  en_wifi = preferences.getUInt("wifi", 1);
  preferences.end();

  if (en_serial > 2) {
    en_serial = 0;
  }

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
  preferences.end();

  /*CONFIGURACION WIFI*/
  preferences.begin("wifi", true);
  // ssid = preferences.getString("ssid", "Flash-PaPeR");
  // password = preferences.getString("password", "Ayanami84");
  ssid = preferences.getString("ssid", "Invitados");
  password = preferences.getString("password", "TCinvitados");
  // ssid = preferences.getString("ssid", "TCSA");
  // password = preferences.getString("password", "ccreto3236TAM");
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

  // Lanza la tarea paralela de monitoreo del boton en el Nucleo 0
  xTaskCreatePinnedToCore(buttonTaskTracker, "ButtonTask", 4096, NULL, 1, NULL,
                          0);
  // ----------------------

  preferences.begin("adc_config", true);
  tADC = (uint16_t)preferences.getInt("tADC", 1);
  if (tADC == 0)
    tADC = 1; // Safety check
  preferences.end();

  if (en_modbus) {
    modbus_set_enabled(true);
    en_serial = 0; // Exclusión mutua
  }

  if (en_serial) {
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH); // La expansora arranca encendida
  }

  if (en_ble) {
    bleScanner.begin();
  }

  /*ACA TENGO QUE PONER EL SENSOR QUE VOY A UTILIZAR*/
  sensor = new DS18B20();

  if (en_modem) {
    asegurarModemEncendido(); // Asegurar que el módem esté encendido y respondiendo a comandos AT
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

  // Inicializacion del sensor
  sensor->begin();

  // Seteo el LED output
  pinMode(LED_PIN, OUTPUT); // Seteo el LED OFF

  /*BOToN AP*/
  pinMode(AP_BUTTON_PIN,
          INPUT_PULLUP); // Boton con logica inversa (presionado = LOW)

  /*LECTURA ID*/
  ident = leer_de_flash("ident", "60000");
  ident.toUpperCase();

  topic1 = "DVL/LILY-GO/" + ident;
  topic1.toUpperCase();
  DVL_PRINT("TOPIC MQTT: ");
  DVL_PRINTLN(topic1);

  /*CONFIGURACIoN WEB SERVER ADMIN*/
  preferences.begin("mqtt", true);
  MQTT_BROKER = preferences.getString(
      // "ip", "192.168.7.24"); // ← solo usa este si no hay guardado
      "ip", "iot.tcsa.com.ar");
  MQTT_PORT = preferences.getInt("port", 1883); // ← idem
  preferences.end();

  /*BROKER MQTT*/
  DVL_PRINT("Broker cargado: ");
  DVL_PRINT(MQTT_BROKER);
  DVL_PRINT("  Puerto: ");
  DVL_PRINTLN(MQTT_PORT);

  // La carga del WiFi se movio al principio del setup para verificar el modo AP

  if (en_wifi == 1 && ssid.length() > 0) {
    conectar_WiFi();
  } else if (en_wifi == 2) {
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
    iniciarBridgeAP();
  } else {
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
  }

  lectura_flash();
  cargarConfiguracionParser();

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

  mqtt.loop();
  unsigned long now = millis();
  unsigned long unahora;
  static unsigned long lastNoDataMessage = 0;
  static unsigned long last_10ms_event = 0;
  static unsigned long last_100ms_event = 0;
  static unsigned long last_1s_event = 0;

  // Estructura principal de tiempo
  if (now - last_1s_event >= 1000) {
    last_1s_event = now;
  }

  if (now - last_100ms_event >= 100) {
    last_100ms_event = now;
    if (++cADC >= tADC) {
      procesarADC(ADCValue);
      cADC = 0;
    }
  }

  if (now - last_10ms_event >= 10) {
    last_10ms_event = now;
  }

  // (La deteccion del boton AP ahora se maneja en la tarea paralela
  // buttonTaskTracker)

  // ========== FASE GPS ==========
  if (faseGPS) {
    if (!en_modem || !en_gps) {
      DVL_PRINTLN("GPS omitido (modem o GPS deshabilitados por configuracion).");
      faseGPS = false;
      faseGPRS_WIFI = true;
      digitalWrite(LED_PIN, false);
    } else {
      DVL_PRINTLN("Entrando a fase GPS bloqueante");
    // Encendemos GPS si no esta encendido
    enableGPS();
    float lat, lon;
    unsigned long tiempoInicioBloqueo = now;

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

      // Parpadeo LED sin delay bloqueante
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink > 300) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastBlink = millis();
        esp_task_wdt_reset();
      }
      esp_task_wdt_reset();
      delay(10); // pequeño delay para no saturar CPU
    }

    // Apagamos GPS y reiniciamos modem antes de cambiar fase
    disableGPS();
    modemRestart();

    //---------------------------------------------------------------------//

    if (!modem.init()) {
      DVL_PRINTLN("Fallo en modem.init() luego de restart");
      contadorErroresModem++;
      DVL_PRINT("Contador de errores de modem: ");
      DVL_PRINTLN(contadorErroresModem);

      if (contadorErroresModem >= limiteErroresModem) {
        DVL_PRINTLN("Se alcanzo el limite de errores. Reiniciando modem...");
        modemRestart(); // Reinicio completo del modem
        contadorErroresModem = 0;
        delay(3000);

        if (!modem.init()) {
          DVL_PRINTLN("Fallo tras reinicio forzado del modem.");
          // Si queres reiniciar toda la placa en este punto, podes hacer:
          ESP.restart();
        } else {
          DVL_PRINTLN("Modem recuperado.");
          faseGPS = false;
          faseGPRS_WIFI = true;
          digitalWrite(LED_PIN, false);
        }
      }
    } else {
      DVL_PRINTLN("Modem iniciado.");
      faseGPS = false;
      faseGPRS_WIFI = true;
      digitalWrite(LED_PIN, false);
    }
    }
  }

  /*ACA SE FIJA SI ESTOY CONECTADO A WIFI O A RED CELULAR*/
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) { // cada 10 segundos
    if (TINY_GSM_USE_WIFI && WiFi.status() == WL_CONNECTED) {
      DVL_PRINTLN("Conectado por WiFi");
    } else if (TINY_GSM_USE_GPRS && modem.isNetworkConnected() &&
               modem.isGprsConnected()) {
      DVL_PRINTLN("Conectado por GPRS");
    } else {
      DVL_PRINTLN("No hay conexion activa");
    }
    lastCheck = millis();
  }

  // ========== FASE GPRS/WIFI ==========
  if (faseGPRS_WIFI) {

    if (now - unahora >= (60 * 1000 * 60)) { // contador 1 hora
      if (en_wifi == 1) {
        TINY_GSM_USE_WIFI = true;
        TINY_GSM_USE_GPRS = false;
      }
    }

    if (TINY_GSM_USE_WIFI == true && TINY_GSM_USE_GPRS == false &&
        WiFi.status() != WL_CONNECTED) {
      mqtt.setClient(espClient);
      conectar_WiFi();

    } else if (TINY_GSM_USE_WIFI == false && TINY_GSM_USE_GPRS == true) {

      mqtt.setClient(gsmClient);
      updateNetworkConnection(modem); // Conexion no bloqueante

      if (!modem.isGprsConnected()) {
        if (millis() - lastGprsConnectAttempt >= gprsConnectInterval) {
          lastGprsConnectAttempt = millis();
          DVL_PRINTLN("Conectando a GPRS...");
          if (modem.gprsConnect(apn.c_str(), gprsUser, gprsPass)) {
            DVL_PRINTLN("GPRS conectado correctamente");
            // MQTT Broker setup
            mqtt.setServer(MQTT_BROKER.c_str(), 1883);
            mqtt.setCallback(mqttCallback);
          } else {
            DVL_PRINTLN("Fallo al conectar GPRS");
          }
        }
      }

      if (modem.isNetworkConnected() && modem.isGprsConnected()) {

        if (!relojActualizado) {
          if (millis() - lastNtpSyncAttempt >= ntpSyncInterval) {
            lastNtpSyncAttempt = millis();
            if (updateClockFromNTP(modem)) {

              relojActualizado = true;
              ultimaActualizacionNTP = now;
              // ... Utiliza la hora sincronizada ...
            } else {
              DVL_PRINTLN("Fallo la actualizacion de NTP");
            }
          }
        } else if (now - ultimaActualizacionNTP >= intervaloNTP) {
          if (updateClockFromNTP(modem)) {
            ultimaActualizacionNTP = now;
          }
        }
      }
    }
  }

  bool hasNetwork = (TINY_GSM_USE_WIFI && WiFi.status() == WL_CONNECTED) ||
                    (TINY_GSM_USE_GPRS && modem.isNetworkConnected() && modem.isGprsConnected());

  if (hasNetwork && !mqtt.connected()) {
    uint32_t t = millis();
    if (t - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = t;
      esp_task_wdt_reset();
      mqttConnect();
      esp_task_wdt_reset();
    }
  }

  if (en_ble) {
    bleScanner.loop();

    if (bleScanner.hasNewData()) {
      DVL_PRINTLN("Detectados nuevos datos BLE, preparando envio MQTT...");
      std::vector<MokoSensorData> bleDataList = bleScanner.getLatestData();

      for (const auto &bleData : bleDataList) {
        // Topic for BLE: DVL/LILY-GO/<ident>/BLE/<MAC>
        String topicBLE = "DVL/LILY-GO/" + ident + "/BLE/" + bleData.mac;
  topicBLE.toUpperCase();

        unsigned long numPkt = obtener_y_avanzar_numero_paquete();

        String jsonBLE = create_mqtt_json_ble(
            topicBLE, ident, printCurrentTime(), bleData, ultimaLat, ultimaLon,
            leer_tension_backup(), leer_tension_principal(), numPkt);

        if (mqtt.connected()) {
          if (publish_mqtt_json(topicBLE, jsonBLE)) {
            mqttUltimaConexionOK = millis();
          } else {
            DVL_PRINTLN("Fallo al publicar BLE online");
          }
        } else {
          flash_save_packet(jsonBLE.c_str());
          DVL_PRINTLN("MQTT desconectado. Datos BLE guardados en flash.");
        }
      }
    }
  }

  /*LOOP CADA 5 SEGUNDOS*/
  if (en_sensor) {
    // ---- LECTURA SENSOR ----
    if (now - last_Sensor_read > Sensor_read_Interval) {
      last_Sensor_read = now;
      float valor = sensor->readValue(); // Lee valor actual del sensor
      valorStr = String(valor, 2);       // Guarda en string para el JSON
      esp_task_wdt_reset();
    }

    /*DE ACa SALE LA LECTURA DEL SENSOR Y SU REINICIO SI SE TILDA*/
    sensor->loop();
  }

  // ---- LECTURA DE FLASH ----
  if (now - last_flash_read > Packets_read_Interval) {

    while (!flash_buffer_empty()) {
      const char *packetStr = flash_get_next_packet();
      esp_task_wdt_reset();

      if (packetStr == nullptr)
        break;
      
      last_flash_read = now;
      String sendTopic = topic1;
      StaticJsonDocument<2048> doc;
      if (!deserializeJson(doc, packetStr) && doc.containsKey("topic")) {
        sendTopic = doc["topic"].as<String>();
      }

      if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
        if (publish_mqtt_json(sendTopic, String(packetStr))) {
          flash_mark_packet_sent();
          mqttUltimaConexionOK = millis(); //  Reset tambien con datos del buffer
        } else {
          break;
        }
      } else if (en_modem && modem.isGprsConnected() && modem.isNetworkConnected() &&
                 mqtt.connected()) {
        if (publish_mqtt_json(sendTopic, String(packetStr))) {
          flash_mark_packet_sent();
          mqttUltimaConexionOK = millis(); //  Reset tambien con datos del buffer
        } else {
          break;
        }
      } else {
        break;
      }
    }
  }

  // ---- ENViO DATOS CADA INTERVALO ----  //
  if (now - lastPublish > publishInterval * 1000) {

    lastPublish = now;

    if (WiFi.status() == WL_CONNECTED) {
      updateClockFromNTP_wifi();
    }

    DVL_PRINT("Dato sensor: ");
    DVL_PRINTLN(valorStr);

    unsigned long numPkt = obtener_y_avanzar_numero_paquete();

    if (en_sensor == 1) {
      float valSensor = valorStr.toFloat();
      if (valSensor >= -20.0 && valSensor <= 50.0) {
        DVL_PRINT("Sensor habilitado. Enviando dato por MQTT...");

        String topicSensor = topic1 + "/DS18B20";
  topicSensor.toUpperCase();

        String jsonsensor = create_mqtt_json_sensor(
            topicSensor, ident, valorStr, printCurrentTime(),
            leer_tension_backup(), leer_tension_principal(), numPkt);

        if (mqtt.connected()) {

          if (topicSensor.length() == 0 || jsonsensor.length() == 0) {
            DVL_PRINTLN(" ERROR: Topico o mensaje MQTT vacio. No se publica.");
          } else {
            if (publish_mqtt_json(topicSensor, jsonsensor)) {
              mqttUltimaConexionOK = millis(); //  Reset al publicar con exito
            }
          }

        } else {
          flash_save_packet(jsonsensor.c_str());

          DVL_PRINTLN(mqtt.connected());

          DVL_PRINTLN(WiFi.status());
        }
      } else {
        DVL_PRINT("Lectura de sensor fuera de rango (-20 a +50), descartada: ");
        DVL_PRINTLN(valorStr);
      }
    }

    if (en_serial == 1) {

      DVL_PRINT("Serial habilitado. Enviando dato por MQTT...");

      String topicSerial = topic1 + "/SERIAL";
  topicSerial.toUpperCase();

      String jsonserial = create_mqtt_json_serial(
          topic1, ident, sensorValues[0], sensorValues[1], sensorValues[2],
          sensorValues[3], sensorValues[4], sensorValues[5], sensorValues[6],
          sensorValues[7], sensorValues[8], sensorValues[9], sensorValues[10],
          sensorValues[11], sensorValues[12], sensorValues[13],
          sensorValues[14], sensorValues[15], printCurrentTime(), ultimaLat,
          ultimaLon, leer_tension_backup(), leer_tension_principal(), numPkt);

      if (mqtt.connected()) {

        if (topicSerial.length() == 0 || jsonserial.length() == 0) {
          DVL_PRINTLN(" ERROR: Topico o mensaje MQTT vacio. No se publica.");
        } else {
          if (publish_mqtt_json(topicSerial, jsonserial)) {
            mqttUltimaConexionOK = millis(); //  Reset al publicar con exito
          }
        }

      } else {

        flash_save_packet(jsonserial.c_str());
        DVL_PRINTLN(mqtt.connected());
        DVL_PRINTLN(WiFi.status());
      }
    }

    if (en_modbus == 1) {

      DVL_PRINT("Modbus habilitado. Enviando dato por MQTT...");

      unsigned long numPkt = obtener_y_avanzar_numero_paquete();
      String topicModbus = topic1 + "/MODBUS";
  topicModbus.toUpperCase();
      String jsonmodbus = create_mqtt_json_modbus(
          topicModbus, ident, printCurrentTime(), ultimaLat, ultimaLon,
          leer_tension_backup(), leer_tension_principal(), numPkt);

      if (mqtt.connected()) {
        if (topicModbus.length() == 0 || jsonmodbus.length() == 0) {
          DVL_PRINTLN(" ERROR: Topico o mensaje MQTT vacio. No se publica.");
        } else {
          if (publish_mqtt_json(topicModbus, jsonmodbus)) {
            mqttUltimaConexionOK = millis();
          }
        }
      } else {
        flash_save_packet(jsonmodbus.c_str());
      }
    }

    if (en_adc == 1) {
      DVL_PRINT("ADC habilitado. Enviando reporte ADC...");
      String topicADC = topic1 + "/ADC";
  topicADC.toUpperCase();
      String jsonadc = create_mqtt_json_adc(
          topicADC, ident, printCurrentTime(), ADCValue[0], ADCValue[1]);

      if (mqtt.connected()) {
        if (publish_mqtt_json(topicADC, jsonadc)) {
          mqttUltimaConexionOK = millis();
        }
      } else {
        flash_save_packet(jsonadc.c_str());
      }
    }

    esp_task_wdt_reset();
  }

  // Keep Alive si no hay datos para transmitir
  if (now - lastNoDataMessage > 5000) {
    String cType = "NO_CONECTADO";
    String cDetail = "N/A";
    String rsrq = "N/A";
    String rsrp = "N/A";
    String rssi = "N/A";

    if (WiFi.status() == WL_CONNECTED) {
      cType = "WIFI";
      cDetail = WiFi.SSID();
      rssi = String(WiFi.RSSI());
    } else if (en_modem && (modem.isGprsConnected() || modem.isNetworkConnected())) {
      cType = "GSM";
      cDetail = getGSMTech();
      getModemSignalInfo(modem, rsrq, rsrp, rssi);
    }

    // Consultar info unica del modem siempre, sin importar si usamos WiFi o GSM
    if (en_modem && (modemIMEI.length() < 10 || modemICCID.length() < 10)) {
      static unsigned long lastModemQuery = 0;
      // Reintentar capturarlos maximo una vez cada 30 segundos para no saturar
      // ni bloquear si no hay chip
      if (lastModemQuery == 0 || (millis() - lastModemQuery > 30000)) {
        modemIMEI = modem.getIMEI();
        modemIMSI = modem.getIMSI();
        modemICCID = modem.getSimCCID();
        modemICCID.toUpperCase();
        lastModemQuery = millis();
      }
    }

    String jsonKeepAlive = create_mqtt_json_keepalive(
        topic1, ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
        rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
        rsrp, rssi, leer_tension_backup(), leer_tension_principal());
    if (mqtt.connected()) {
      publish_mqtt_json(topic1, jsonKeepAlive);
    }
    lastNoDataMessage = now;
  }

  if (mqttActivo && millis() - mqttUltimaConexionOK > MQTT_TIMEOUT) {
    DVL_PRINTLN("🕒 Tiempo sin reconexion MQTT superado. Reiniciando...");
    delay(1000);
    ESP.restart();
  }

  // ---- OTRAS FUNCIONES ----

  escucharComandos();

  // Solo leo SerialSecundario si está habilitado EN_SERIAL
  if (en_serial == 1) {
    leerSensorSerial(SensorSerial);
  } else if (en_serial == 2) {
    leerYRetransmitirSerial(SensorSerial);
  }

  // Solo ciclo Modbus si está habilitado
  if (en_modbus && modbus_get_enabled()) {
    modbus_loop();
  }

  if (en_wifi == 2) {
    mantenerBridgeAP();
  }
  actualizarLED();
  esp_task_wdt_reset(); // Alimenta el WDT
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
  if (en_modem && (modem.isGprsConnected() || modem.isNetworkConnected())) {
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
