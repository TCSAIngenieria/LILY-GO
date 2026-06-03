#define SerialMon Serial

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
#include "MQTT.h"
#include "Modbus.h"
#include "SerialSecundario.h"
#include "WebServerConfig.h"


WiFiClient espClient;

// Cliente MQTT comun para los dos tipos de conectividad
PubSubClient mqtt(espClient);

HardwareSerial SensorSerial(2); // UART2

#define LED_PIN 2
#define WDT_TIMEOUT 120 // segundos para que reinicie por watchdog

String versionado = "V05.01.01";

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
extern uint en_ble;
extern uint en_adc;
extern uint en_modem;
extern uint en_gps;
// WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;

// Valores puerto serial secundario;
extern String sensorValues[16];



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
  SensorSerial.begin(4800, SERIAL_8N1, 32,
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
  sensor->begin();

  // Seteo el LED output
  pinMode(LED_PIN, OUTPUT); // Seteo el LED OFF

  /*BOToN AP*/
  pinMode(AP_BUTTON_PIN,
          INPUT_PULLUP); // Boton con logica inversa (presionado = LOW)

  /*LECTURA ID*/
  ident = leer_de_flash("ident", "60000");
  ident.toUpperCase();

  topic1 = "DVL/NODEMCU/" + ident;
  topic1.toUpperCase();
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
  String cType = "";
  String cDetail = "";
  String rssi = "";
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

// ========== FASE GPRS/WIFI ==========
  if (WiFi.status() != WL_CONNECTED) {
    conectar_WiFi();
  }

  bool hasNetwork = (WiFi.status() == WL_CONNECTED);

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
        // Topic for BLE: DVL/NODEMCU/<ident>/BLE/<MAC>[/<frameType>]
        String topicBLE = "DVL/NODEMCU/" + ident + "/BLE/" + bleData.mac;
  topicBLE.toUpperCase();
        switch (bleData.frameType) {
        case 0x40:
          topicBLE += "/Device_Info";
          break;
        case 0x50:
          topicBLE += "/iBeacon";
          break;
        case 0x60:
          topicBLE += "/3-axis_Acc";
          break;
        case 0x70:
          topicBLE += "/T_and_H";
          break;
        default:
          if (bleData.frameType != 0) {
            char ftBuf[10];
            sprintf(ftBuf, "/0x%02X", bleData.frameType);
            topicBLE += String(ftBuf);
          }
          break;
        }

        unsigned long numPkt = obtener_y_avanzar_numero_paquete();

        String jsonBLE = create_mqtt_json_ble(
            topicBLE, ident, printCurrentTime(), bleData, ultimaLat, ultimaLon,
            leer_tension_bateria(), leer_tension_principal(), numPkt);

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
      } else {
        break;
      }
    }
  }

  // ---- ENViO DATOS CADA INTERVALO ----  //
  if (now - lastPublish > publishInterval * 1000) {

    lastPublish = now;

    if (WiFi.status() == WL_CONNECTED) {
      cType = "WIFI";
      cDetail = WiFi.SSID();
      rssi = String(WiFi.RSSI());
    }

    String jsonKeepAlive = create_mqtt_json_keepalive(
        topic1, ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
        rebootCount, cType, cDetail, "", "", "", "",
        "", rssi);
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
  if (en_serial) {
    leerSensorSerial(SensorSerial);
  }

  // Solo ciclo Modbus si está habilitado
  if (en_modbus && modbus_get_enabled()) {
    modbus_loop();
  }

  actualizarLED();
  esp_task_wdt_reset(); // Alimenta el WDT
}

void actualizarLED() {
  unsigned long now = millis();

  if (mqttActivo && mqtt.connected() &&
      (now - mqttUltimaConexionOK <= MQTT_TIMEOUT)) {
    int seqMQTT = now % 5000;
    if (seqMQTT >= 0 && seqMQTT < 100) {
      digitalWrite(LED_PIN, LOW);
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
    return;
  }

  int seq = now % 2000;
  if (WiFi.status() == WL_CONNECTED) {
    if ((seq >= 0 && seq < 100) || (seq > 200 && seq < 300) ||
        (seq > 400 && seq < 500)) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    return;
  }

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
  } else {
    DVL_PRINTLN("No se pudo conectar a WiFi. Esperando boton...");
    wifiConfigurado = false;
  }
}
