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
#include "WebServerConfig.h"

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

String versionado = "V03.03.01";

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

// WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;

// Valores puerto serial secundario;
extern String sensorValues[16];

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
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
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
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Parpadeo constante
      delay(500);
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
  sensor->begin();

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

      TINY_GSM_USE_WIFI = true;
      TINY_GSM_USE_GPRS = false;
    }

    if (TINY_GSM_USE_WIFI == true && TINY_GSM_USE_GPRS == false &&
        WiFi.status() != WL_CONNECTED) {
      mqtt.setClient(espClient);
      conectar_WiFi();

    } else if (TINY_GSM_USE_WIFI == false && TINY_GSM_USE_GPRS == true) {

      mqtt.setClient(gsmClient);
      updateNetworkConnection(modem); // Conexion no bloqueante

      if (!modem.isGprsConnected()) {
        DVL_PRINTLN("Intentando conectar GPRS...");
        if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
          DVL_PRINTLN("GPRS conectado correctamente");
          // MQTT Broker setup
          mqtt.setServer(MQTT_BROKER.c_str(), 1883);
          mqtt.setCallback(mqttCallback);
        } else {
          DVL_PRINTLN("Fallo al conectar GPRS");
        }
      }

      if (modem.isNetworkConnected() && modem.isGprsConnected()) {

        if (!relojActualizado) {
          if (updateClockFromNTP(modem)) {

            relojActualizado = true;
            ultimaActualizacionNTP = now;
            // ... Utiliza la hora sincronizada ...
          } else {
            DVL_PRINTLN("Fallo la actualizacion de NTP");
          }
        } else if (now - ultimaActualizacionNTP >= intervaloNTP) {
          if (updateClockFromNTP(modem)) {
            ultimaActualizacionNTP = now;
          }
        }
      }
    }
  }

  if (!mqtt.connected()) {
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
        // Topic for BLE: DVL/LILY-GO/<ident>/BLE/<MAC>[/<frameType>]
        String topicBLE = "DVL/LILY-GO/" + ident + "/BLE/" + bleData.mac;
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
      const char *packet = flash_get_next_packet();
      esp_task_wdt_reset();

      if (packet == nullptr)
        break;
      last_flash_read = now;
      String packetStr = String(packet);
      if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
        if (publish_mqtt_json(topic1, packetStr)) {
          flash_mark_packet_sent();
          mqttUltimaConexionOK =
              millis(); //  Reset tambien con datos del buffer

        } else {
          break;
        }
      }
      break;
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

        String jsonsensor = create_mqtt_json_sensor(
            topicSensor, ident, valorStr, printCurrentTime(),
            leer_tension_bateria(), leer_tension_principal(), numPkt);

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

      String jsonserial = create_mqtt_json_serial(
          topic1, ident, sensorValues[0], sensorValues[1], sensorValues[2],
          sensorValues[3], sensorValues[4], sensorValues[5], sensorValues[6],
          sensorValues[7], sensorValues[8], sensorValues[9], sensorValues[10],
          sensorValues[11], sensorValues[12], sensorValues[13],
          sensorValues[14], sensorValues[15], printCurrentTime(), ultimaLat,
          ultimaLon, leer_tension_bateria(), leer_tension_principal(), numPkt);

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
      String jsonmodbus = create_mqtt_json_modbus(
          topic1, ident, printCurrentTime(), ultimaLat, ultimaLon,
          leer_tension_bateria(), leer_tension_principal(), numPkt);

      if (mqtt.connected()) {
        if (topic1.length() == 0 || jsonmodbus.length() == 0) {
          DVL_PRINTLN(" ERROR: Topico o mensaje MQTT vacio. No se publica.");
        } else {
          if (publish_mqtt_json(topic1, jsonmodbus)) {
            mqttUltimaConexionOK = millis();
          }
        }
      } else {
        flash_save_packet(jsonmodbus.c_str());
      }
    }

    if (en_ble == 1) {
      // Logic for BLE MQTT Report
      if (bleScanner.hasNewData()) {
        std::vector<MokoSensorData> bleDataList = bleScanner.getLatestData();

        for (const auto &data : bleDataList) {
          unsigned long numPkt = obtener_y_avanzar_numero_paquete();

          DVL_PRINT("BLE Data found. Temp: ");
          DVL_PRINT(data.temperature);
          DVL_PRINTLN(" Sending MQTT...");

          // Topic: DVL/LILY-GO/<ident>/BLE/<MAC>[/<frameType>]
          String topicBLE = "DVL/LILY-GO/" + ident + "/BLE/" + data.mac;
          switch (data.frameType) {
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
            if (data.frameType != 0) {
              char ftBuf[10];
              sprintf(ftBuf, "/0x%02X", data.frameType);
              topicBLE += String(ftBuf);
            }
            break;
          }

          String jsonble = create_mqtt_json_ble(
              topicBLE, ident, printCurrentTime(), data, ultimaLat, ultimaLon,
              leer_tension_bateria(), leer_tension_principal(), numPkt);

          if (mqtt.connected()) {
            if (publish_mqtt_json(topicBLE, jsonble)) {
              mqttUltimaConexionOK = millis();
            }
          } else {
            flash_save_packet(jsonble.c_str());
          }
        }
      }
    }

    if (en_adc == 1) {
      DVL_PRINT("ADC habilitado. Enviando reporte ADC...");
      String jsonadc = create_mqtt_json_adc(
          ident, printCurrentTime(), ADCValue[0], ADCValue[1], ADCValue[2]);

      String topicADC = topic1 + "/ADC";
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
    } else if (modem.isGprsConnected() || modem.isNetworkConnected()) {
      cType = "GSM";
      cDetail = getGSMTech();
      getModemSignalInfo(modem, rsrq, rsrp, rssi);
    }

    // Consultar info unica del modem siempre, sin importar si usamos WiFi o GSM
    if (modemIMEI.length() < 10 || modemICCID.length() < 10) {
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
        ident, printCurrentTime(), ultimaLat, ultimaLon, versionado,
        rebootCount, cType, cDetail, modemIMEI, modemIMSI, modemICCID, rsrq,
        rsrp, rssi);
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

  // Si esta en modo configuracion (AP) → no tocamos nada (ya se maneja en
  // loop del AP)
  if (!wifiConfigurado &&
      (ssid.length() == 0 || digitalRead(AP_BUTTON_PIN) == LOW))
    return;

  // MQTT activo y dentro de los 5 minutos desde la ultima conexion → LED
  // fijo
  if (mqttActivo && (now - mqttUltimaConexionOK <= MQTT_TIMEOUT)) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  // Conectado a WiFi o GPRS pero sin conexion MQTT → doble parpadeo rapido
  // cada 2 segundos
  if (((WiFi.status() == WL_CONNECTED) || (modem.isGprsConnected())) &&
      !mqtt.connected()) {
    static int blinkCount = 0;
    static unsigned long blinkStart = 0;

    if (now - blinkStart >= 2000) {
      blinkStart = now;
      blinkCount = 0;
    }

    if (blinkCount < 2 && now - ledTimer >= 200) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      ledTimer = now;
      blinkCount++;
    }

    return;
  }

  // No hay WiFi o GPRS → LED apagado
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
