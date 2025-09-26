#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

#define SerialAT Serial1
#define SerialMon Serial

#include <TinyGsmClient.h>
#include <SPI.h>
//#include <SD.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <Preferences.h>
#include <WebServer.h>

#include "DS18B20.h"
#include "Flash.h"
#include "Comandos.h"
#include "SerialSecundario.h"
#include "MQTT.h"
#include "Fechayhora.h"
#include "GNSS.h"
#include "WebServerConfig.h"
#include "ADC.h"
#include "FOTA.h"
#include "GPRS.h"

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif



WiFiClient espClient;
TinyGsmClient gsmClient(modem);

// Cliente MQTT común para los dos tipos de conectividad
PubSubClient mqtt(espClient);  // lo inicializamos con uno cualquiera

HardwareSerial SensorSerial(2); // UART2

#define LED_PIN 2
#define WDT_TIMEOUT 120  // segundos para que reinicie por watchdog


String versionado = "V1";

/*VARIABLES MQTT*/
unsigned long ledTimer = 0;
bool ledState = false;
bool mqttActivo = false;
unsigned long mqttUltimaConexionOK = 0;
const unsigned long MQTT_TIMEOUT = 300000; // 5 minutos

unsigned long lastMqttResubscribe = 0;
const unsigned long mqttResubscribeInterval = 10000; // cada 10 segundos


//Variables generales
String ident="";
String topic1;
unsigned long numeroPaquete = 0;

//Habilitación módulos
extern uint en_sensor;
extern uint en_serial;


//WIFI
extern bool wifiConfigurado;
unsigned long configure_wifi_time = 0;

Preferences preferences;


//Valores puerto serial secundario;
extern String sensorValues[16];



// Credenciales GPRS
const char apn[]  = "igprs.claro.com.ar";     // APN
const char gprsUser[] = "";
const char gprsPass[] = "";


//conectividad
bool TINY_GSM_USE_GPRS = true;           // uso del gprs
bool TINY_GSM_USE_WIFI = false;         // uso del wifi


// Control de fases
bool faseGPS = true;           // Empezamos buscando latitud/longitud
bool faseGPRS_WIFI = false;         // Activamos GPRS luego del GPS
bool relojActualizado = false; // Indica si ya se actualizó el reloj desde NTP

/*Tiempo de búsqueda de latitud y longitud de GPS al inicio*/
unsigned long tiempoInicioGPS = 0;
const unsigned long tiempoLimiteGPS = 0UL * 60UL * 1000UL; // 1 minutos

///*Tiempo de actualización de Hora*/
unsigned long ultimaActualizacionNTP = 0;
const unsigned long intervaloNTP = 8UL * 60UL * 60UL * 1000UL; // 8 horas

/*Variables para reinicio de modem si no se inicia bien*/
int contadorErroresModem = 0;
const int limiteErroresModem = 5;



/* Posición  */
extern String ultimaLat;
extern String ultimaLon;

/* Variable tiempo intento de conección wifi */
unsigned long startAttemptTime = millis();

/* Variables para envío de datos por mqtt */
unsigned long lastPublish = 0;
extern unsigned long publishInterval;  //intervalo de publicación del dato //60 segundos por default//
unsigned long lastReconnectAttempt = 0;

/*BOTÓN MODO AP*/
#define AP_BUTTON_PIN 0  // GPIO del botón (ejemplo: GPIO 0)
#define BUTTON_PRESS_TIME 5000  // 5 segundos
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;

/* Sensor */
SensorInterface* sensor;;
unsigned long last_Sensor_read = 0;
const unsigned long Sensor_read_Interval= 5000; //intervalo de lectura de sensor //5 segundos por default
String valorStr = "";

/*Lectura de paquetes en flash*/
const unsigned long Packets_read_Interval= 2000;
unsigned long last_flash_read = 0;






void setup() {
  SerialMon.begin(115200);  //puerto serial primario
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
  SensorSerial.begin(115200, SERIAL_8N1, 32, 33); // PUERTO SERIAL EXTERNO   RX=GPIO32, TX=GPIO33


  /*ACA TENGO QUE PONER EL SENSOR QUE VOY A UTILIZAR*/
  sensor = new DS18B20();


  modemPowerOn();   // Enciendo el modem celular

  static const esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // ambos cores
    .trigger_panic = true
  };

  

  // Inicializa el watchdog
  esp_task_wdt_delete(NULL);  // Elimina la tarea actual del watchdog anterior
  esp_task_wdt_deinit();      // Desactiva completamente el WDT actual
  esp_task_wdt_init(&wdt_config);  // Ahora sí, lo inicializa con 120 segundos
  esp_task_wdt_add(NULL);     // Agrega la tarea actual al nuevo WDT
  
  flash_init();
  initADC();
  
  

  //Inicialización del sensor
  sensor->begin();
  
  // Seteo el LED output
  pinMode(LED_PIN, OUTPUT); // Seteo el LED OFF


  /*BOTÓN AP*/
  pinMode(AP_BUTTON_PIN, INPUT_PULLUP);  // Botón con lógica inversa (presionado = LOW)

  /*LECTURA ID*/
  ident = leer_de_flash("ident", "60000");

  topic1 = "DVL/LilyGO/" + ident;
  Serial.print("TOPIC MQTT: ");
  Serial.println(topic1);


  /*CONFIGURACIÓN WEB SERVER ADMIN*/
  preferences.begin("mqtt", true);
  MQTT_BROKER = preferences.getString("ip", "iot.tcsa.com.ar");  // ← solo usa este si no hay guardado
  MQTT_PORT = preferences.getInt("port", 1883);                // ← idem
  preferences.end();

  /*BROKER MQTT*/
  Serial.print("Broker cargado: ");
  Serial.print(MQTT_BROKER);
  Serial.print("  Puerto: ");
  Serial.println(MQTT_PORT);


  /*CONFIGURACION WIFI*/
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "Invitados");
  password = preferences.getString("password", "TCinvitados");
  preferences.end();

  if (ssid.length() > 0) {
  conectar_WiFi();
}


  lectura_flash();
  cargarConfiguracionParser();

  
  /*Configuración MQTT*/
  mqtt.setSocketTimeout(60);   // Espera hasta 60 segundos para conectarse
  mqtt.setKeepAlive(60);       // Envia un ping cada 60 segundos si no hay actividad
  mqtt.setBufferSize(512);
  mqtt.setServer(MQTT_BROKER.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);


  preferences.begin("fota", true);
  String storedURL = preferences.getString("url", "");
  preferences.end();

  preferences.begin("enables", true); //modo lectura
  en_sensor = preferences.getUInt("sensor", 0);
  en_serial = preferences.getUInt("serial", 0);
  preferences.end();

  if(en_serial){
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);  // La expansora arranca encendida
    }


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




  


// ---- VERIFICAR BOTÓN CONFIG ----
  if ((digitalRead(AP_BUTTON_PIN) == LOW) || (ssid.length() == 0)) {
    if (!buttonWasPressed) {
      buttonPressStartTime = now;
      buttonWasPressed = true;
    } else if (now - buttonPressStartTime >= BUTTON_PRESS_TIME) {
      Serial.println("Botón presionado 5 segundos. Entrando en modo configuración...");
      mqtt.disconnect();
      WiFi.disconnect(true);  // Borra configuración WiFi
      delay(500);
      wifiConfigurado = false;
      iniciarModoConfiguracion();
      iniciarWebServerPrivado();

      // Bucle del modo AP
      while (true) {
        
        server.handleClient();
        adminServer.handleClient();
        
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(500);
        esp_task_wdt_reset();
      }
    }
  } else {
    buttonWasPressed = false;
  }


  // ========== FASE GPS ==========  
  if (faseGPS) {
    Serial.println("Entrando a fase GPS bloqueante");
    Serial.println("punto0");
    // Encendemos GPS si no está encendido
     enableGPS();
Serial.println("punto0.1");
    float lat, lon;
    unsigned long tiempoInicioBloqueo = now;

    bool fix_conseguido = false;
    
while (!fix_conseguido && millis() - tiempoInicioBloqueo < tiempoLimiteGPS) {
  esp_task_wdt_reset();
  Serial.println("punto1");
  if (modem.getGPS(&lat, &lon)) {
    fix_conseguido = true;
    Serial.println("punto2");
    String nuevaLat = String(lat, 6);
    String nuevaLon = String(lon, 6);
    setLocationValid(true);

    setLatitude(nuevaLat);
    setLongitude(nuevaLon);
    ultimaLat = nuevaLat;
    ultimaLon = nuevaLon;

    Serial.println("GPS FIX conseguido:");
    Serial.println("Latitud actual: " + ultimaLat);
    Serial.println("Longitud actual: " + ultimaLon);

    guardar_en_flash("lat", nuevaLat);
    guardar_en_flash("lon", nuevaLon);
    esp_task_wdt_reset();
  }

  // Parpadeo LED sin delay bloqueante
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 300) {
    Serial.println("punto3");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
    esp_task_wdt_reset();
  }
  Serial.println("punto4");
  esp_task_wdt_reset();
  delay(10);  // pequeño delay para no saturar CPU
}
Serial.println("punto5");
    // Apagamos GPS y reiniciamos modem antes de cambiar fase
    disableGPS();
    modemRestart();

//---------------------------------------------------------------------//
    

    if (!modem.init()) {
  Serial.println("Fallo en modem.init() luego de restart");
  contadorErroresModem++;
  Serial.print("Contador de errores de modem: ");
  Serial.println(contadorErroresModem);

  if (contadorErroresModem >= limiteErroresModem) {
    Serial.println("Se alcanzó el límite de errores. Reiniciando módem...");
    modemRestart();        // Reinicio completo del módem
    contadorErroresModem = 0;
    delay(3000);
    
    if (!modem.init()) {
      Serial.println("Fallo tras reinicio forzado del módem.");
      // Si querés reiniciar toda la placa en este punto, podés hacer:
       ESP.restart();
    } else {
      Serial.println("Módem recuperado.");
      faseGPS = false;
      faseGPRS_WIFI = true;
      digitalWrite(LED_PIN, false);
    }
  }
}else {
      Serial.println("Módem iniciado.");
      faseGPS = false;
      faseGPRS_WIFI = true;
      digitalWrite(LED_PIN, false);

    
  }
  }



/*ACA SE FIJA SI ESTOY CONECTADO A WIFI O A RED CELULAR*/
  static unsigned long lastCheck = 0;
if (millis() - lastCheck > 10000) {  // cada 10 segundos
  if (TINY_GSM_USE_WIFI && WiFi.status() == WL_CONNECTED) {
    Serial.println("🟢 Conectado por WiFi");
  } else if (TINY_GSM_USE_GPRS && modem.isNetworkConnected() && modem.isGprsConnected()) {
    Serial.println("🟠 Conectado por GPRS");
  } else {
    Serial.println("🔴 No hay conexión activa");
  }
  lastCheck = millis();
}

  

  // ========== FASE GPRS/WIFI ==========
  if (faseGPRS_WIFI) {


  if (now - unahora >= (60*1000*60)) { //contador 1 hora

    TINY_GSM_USE_WIFI = true;
    TINY_GSM_USE_GPRS = false;
    
  }

  if (TINY_GSM_USE_WIFI == true && TINY_GSM_USE_GPRS == false && WiFi.status() != WL_CONNECTED)
{
  mqtt.setClient(espClient);
  conectar_WiFi();
  
  
  
}
  else if (TINY_GSM_USE_WIFI == false && TINY_GSM_USE_GPRS == true)
{
  mqtt.setClient(gsmClient);

  updateNetworkConnection(modem); // Conexión no bloqueante



if (!modem.isGprsConnected()) {
  Serial.println("Intentando conectar GPRS...");
  if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
    Serial.println("GPRS conectado correctamente");
    // MQTT Broker setup
    mqtt.setServer(MQTT_BROKER.c_str(), 1883);
    mqtt.setCallback(mqttCallback);
  } else {
    Serial.println("Fallo al conectar GPRS");
  }
}


if (modem.isNetworkConnected() && modem.isGprsConnected()) {
      
    
      if (!relojActualizado) {  
        if (updateClockFromNTP(modem)) {
    
    relojActualizado = true;
    ultimaActualizacionNTP = now;
    // ... Utiliza la hora sincronizada ...
  } else {
    Serial.println("Falló la actualización de NTP");
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



/*LOOP CADA 5 SEGUNDOS*/




if (en_sensor){  
// ---- LECTURA SENSOR ----
  if (now - last_Sensor_read > Sensor_read_Interval) {
  last_Sensor_read = now;



  float valor = sensor->readValue();  // Lee valor actual del sensor
  valorStr = String(valor, 2);        // Guarda en string para el JSON



  esp_task_wdt_reset();
}


/*DE ACÁ SALE LA LECTURA DEL SENSOR Y SU REINICIO SI SE TILDA*/


  sensor->loop();
}


 // ---- LECTURA DE FLASH ----
  if (now - last_flash_read > Packets_read_Interval) {
    
    while (!flash_buffer_empty()) {
      const char* packet = flash_get_next_packet();
      esp_task_wdt_reset();
      
      
      if (packet == nullptr) break;
      last_flash_read = now;
      String packetStr = String(packet);
      if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
        if (publish_mqtt_json(topic1, packetStr)) {
          flash_mark_packet_sent();
          mqttUltimaConexionOK = millis();  // ✅ Reset también con datos del buffer
          
        } else {
          break;
          
        }
      }
     break;
    }
  }




// ---- ENVÍO DATOS CADA INTERVALO ----  //
  if (now - lastPublish > publishInterval * 1000) {
    
    lastPublish = now;
    
    if (WiFi.status() == WL_CONNECTED){
    updateClockFromNTP_wifi();
    }
    
    Serial.print("Dato sensor: ");
Serial.println(valorStr);

    

unsigned long numPkt = obtener_y_avanzar_numero_paquete();    

if (en_sensor==1){
String jsonsensor = create_mqtt_json_sensor(
  topic1, ident, valorStr, printCurrentTime(), ultimaLat, ultimaLon,
  leer_tension_bateria(), leer_tension_principal(),
  numPkt
);

if (mqtt.connected()) {

      if (topic1.length() == 0 || jsonsensor.length() == 0) {
        Serial.println("🛑 ERROR: Tópico o mensaje MQTT vacío. No se publica.");
      } else {
        if (publish_mqtt_json(topic1, jsonsensor)) {
      mqttUltimaConexionOK = millis();  // ✅ Reset al publicar con éxito
          }
        }
        
    }else {
      flash_save_packet(jsonsensor.c_str());
      
      
      Serial.println(mqtt.connected());
      
      Serial.println(WiFi.status());
    }


}else if (en_serial==1){
String jsonserial = create_mqtt_json_serial(
  topic1, ident, sensorValues[0], sensorValues[1],sensorValues[2], sensorValues[3], sensorValues[4], sensorValues[5], sensorValues[6], sensorValues[7], sensorValues[8], sensorValues[9],sensorValues[10],
  sensorValues[11], sensorValues[12], sensorValues[13], sensorValues[14], sensorValues[15], printCurrentTime(), ultimaLat, ultimaLon,
  leer_tension_bateria(), leer_tension_principal(),
  numPkt
);

if (mqtt.connected()) {

      if (topic1.length() == 0 || jsonserial.length() == 0) {
        Serial.println("🛑 ERROR: Tópico o mensaje MQTT vacío. No se publica.");
      } else {
        if (publish_mqtt_json(topic1, jsonserial)) {
      mqttUltimaConexionOK = millis();  // ✅ Reset al publicar con éxito
          }
        }
        
    }else {
      flash_save_packet(jsonserial.c_str());
      
      
      Serial.println(mqtt.connected());
      
      Serial.println(WiFi.status());
    }

}

    esp_task_wdt_reset();
  }



if (mqttActivo && millis() - mqttUltimaConexionOK > MQTT_TIMEOUT) {
  Serial.println("🕒 Tiempo sin reconexión MQTT superado. Reiniciando...");
  delay(1000);
  ESP.restart();
}



  // ---- OTRAS FUNCIONES ----
  
  escucharComandos();
  leerSensorSerial(SensorSerial);
  actualizarLED();
  esp_task_wdt_reset();  // Alimenta el WDT
  
  }







void actualizarLED() {
  unsigned long now = millis();

  // Si está en modo configuración (AP) → no tocamos nada (ya se maneja en loop del AP)
  if (!wifiConfigurado && (ssid.length() == 0 || digitalRead(AP_BUTTON_PIN) == LOW)) return;

  // MQTT activo y dentro de los 5 minutos desde la última conexión → LED fijo
  if (mqttActivo && (now - mqttUltimaConexionOK <= MQTT_TIMEOUT)) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  // Conectado a WiFi o GPRS pero sin conexión MQTT → doble parpadeo rápido cada 2 segundos
  if (((WiFi.status() == WL_CONNECTED)|| (modem.isGprsConnected())) && !mqtt.connected()) {
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
  if (numeroPaquete >= 7000) numeroPaquete = 0;
  return actual;
} 













  void conectar_WiFi(){

  Serial.println("Intentando conectar a WiFi guardada...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado a WiFi!");
    wifiConfigurado = true;
    TINY_GSM_USE_WIFI = true;
    TINY_GSM_USE_GPRS = false;
  } else {
    Serial.println("No se pudo conectar a WiFi. Esperando botón...");
    wifiConfigurado = false;
    TINY_GSM_USE_WIFI = false;
    TINY_GSM_USE_GPRS = true;
  }
  }
  
