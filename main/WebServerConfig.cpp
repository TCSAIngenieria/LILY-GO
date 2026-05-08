#include "WebServerConfig.h"
#include "Debug.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "SensorInterface.h"


extern Preferences preferences;
WebServer server(80);         //web server
WebServer adminServer(8080);  //web server admin

String ssid = "";
String password = "";
bool wifiConfigurado = false;





/*--------------------------------WEB SERVER CLIENTE-----------------------------------*/



// Funcion para generar barras Unicode segun RSSI
String signalBars(int rssi) {
  String bars;
  if (rssi > -50)
    bars = "▂▃▄▅▆▇";
  else if (rssi > -60)
    bars = "▂▃▄▅▆";
  else if (rssi > -70)
    bars = "▂▃▄▅";
  else if (rssi > -80)
    bars = "▂▃▄";
  else if (rssi > -90)
    bars = "▂▃";
  else
    bars = "▂";

  return "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<small>" + bars + "</small>";
}

void handleRoot() {
  int n = WiFi.scanNetworks();
  String opcionesRedes = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    opcionesRedes += "<option value=\"" + ssid + "\">" + ssid + signalBars(rssi) + "</option>";
  }

  String page = R"rawliteral(
<html>
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background: #f4f6f8;
        font-family: 'Segoe UI', sans-serif;
        color: #333;
        text-align: center;
        padding: 0 20px;
      }
      .container {
        max-width: 400px;
        margin: 60px auto;
        padding: 30px;
        background: #fff;
        border-radius: 16px;
        box-shadow: 0 4px 20px rgba(0,0,0,0.1);
      }
      .title {
        font-size: 36px;
        font-weight: 700;
        margin-bottom: 10px;
        color: #007acc;
      }
      h1 {
        font-size: 22px;
        margin-bottom: 25px;
        font-weight: 400;
      }
      select, input[type='password'], input[type='text'] {
        width: 100%;
        font-size: 18px;
        padding: 12px;
        margin: 12px 0;
        border: 1px solid #ccc;
        border-radius: 8px;
        box-sizing: border-box;
      }
      input[type='submit'] {
        background: #007acc;
        color: white;
        border: none;
        padding: 14px 20px;
        font-size: 20px;
        border-radius: 8px;
        cursor: pointer;
        transition: background 0.3s ease;
        width: 100%;
      }
      input[type='submit']:hover {
        background: #005f99;
      }
      .eye-icon {
        position: absolute;
        right: 12px;
        top: 50%;
        transform: translateY(-50%);
        cursor: pointer;
        font-size: 18px;
        user-select: none;
      }
      .password-wrapper {
        position: relative;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <div class="title">DVL IoT</div>
      <h1>Configurar WiFi</h1>
      <form action="/save" method="POST">
        <select name="ssid">
          <option value="">Seleccione una red</option>
)rawliteral";

  page += opcionesRedes;

  page += R"rawliteral(
        </select><br>
        <div class="password-wrapper">
          <input type="password" name="password" id="wifiPassword" placeholder="Contraseña WiFi" style="padding-right: 40px;">
          <span class="eye-icon" onclick="togglePassword()">👁️</span>
        </div>
        <input type="submit" value="Guardar">
      </form>
    </div>
    <script>
      function togglePassword() {
        var input = document.getElementById("wifiPassword");
        input.type = input.type === "password" ? "text" : "password";
      }
    </script>
  </body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    ssid = server.arg("ssid");
    password = server.arg("password");

    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();

    server.send(200, "text/html", "<h1>Guardado! Intentando conectar...</h1>");

    delay(2000);
    // Apagar modo AP
    WiFi.softAPdisconnect(true);

    // Intentar conectar
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(500);
      DVL_PRINT(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      DVL_PRINTLN("WiFi conectado!");
      wifiConfigurado = true;
      ESP.restart();
    } else {
      DVL_PRINTLN("No pudo conectar, reiniciando sistema...");
      ESP.restart();
    }
  } else {
    server.send(400, "text/plain", "Faltan datos");
  }
}

void iniciarModoConfiguracion() {
  DVL_PRINTLN("Modo configuracion: arranca AP");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("DVL AP");

  IPAddress IP = WiFi.softAPIP();
  DVL_PRINT("AP IP: ");
  DVL_PRINTLN(IP);


  if (MDNS.begin("wifi")) {
    DVL_PRINTLN("mDNS responder iniciado: wifi.local");
  } else {
    DVL_PRINTLN("Error iniciando mDNS");
  }


  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}

/*____________________________________________________________________________________________________________*/


/*--------------------------------WEB SERVER ADMIN-----------------------------------*/

void handleRootPrivado() {
  preferences.begin("mqtt", true);
  String ip = preferences.getString("ip", "192.168.1.100");
  int port = preferences.getInt("port", 1883);
  preferences.end();

  String page = R"rawliteral(
<html>
<head>
  <meta charset="UTF-8">
  <style>
    body { text-align: center; font-family: Arial; }
    h1 { font-size: 36px; }
    input { font-size: 24px; padding: 10px; margin: 10px; }
    .sensor-box {
      background: #eef;
      padding: 20px;
      margin: 20px auto;
      width: 280px;
      border-radius: 10px;
      font-size: 24px;
      font-weight: bold;
    }
  </style>
  <script>
    function updateValor() {
      fetch('/valor')
        .then(response => response.text())
        .then(data => {
          document.getElementById('valor').innerText = data;
        })
        .catch(err => {
          document.getElementById('valor').innerText = "Error";
        });
    }

    setInterval(updateValor, 3000);
    window.onload = updateValor;
  </script>
</head>
<body>
  <h1>Configuracion MQTT</h1>
  <form action="/save" method="POST">
    IP Broker: <input type="text" name="ip" value=")rawliteral"
                + ip + R"rawliteral("><br>
    Puerto: <input type="number" name="port" value=")rawliteral"
                + String(port) + R"rawliteral("><br>
    <input type="submit" value="Guardar">
  </form>

  <div class="sensor-box">
    Valor del sensor: <span id="valor">--</span>
  </div>
</body>
</html>
)rawliteral";

  adminServer.send(200, "text/html", page);
}

void handleSavePrivado() {
  if (adminServer.hasArg("ip") && adminServer.hasArg("port")) {
    String ip = adminServer.arg("ip");
    int port = adminServer.arg("port").toInt();

    preferences.begin("mqtt", false);
    preferences.putString("ip", ip);
    preferences.putInt("port", port);
    preferences.end();

    adminServer.send(200, "text/html", "<h1>Guardado! <a href='/'>Volver</a></h1>");
    DVL_PRINTLN("NUEVOS PARaMETROS MQTT");
    ESP.restart();
  } else {
    adminServer.send(400, "text/plain", "Faltan datos");
  }
}

void handleValorSensor() { adminServer.send(200, "text/plain", "N/A"); }

void iniciarWebServerPrivado() {
  adminServer.on("/", handleRootPrivado);
  adminServer.on("/save", HTTP_POST, handleSavePrivado);
  adminServer.on("/valor", HTTP_GET, handleValorSensor);  // 👈 esta linea
  adminServer.begin();
  DVL_PRINTLN("Servidor privado en puerto 8080 listo.");
}

