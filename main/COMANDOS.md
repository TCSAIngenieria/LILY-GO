# Manual Técnico de Comandos CLI / MQTT — IoT Lite

## 1. Arquitectura de Transporte y Sintaxis General

Todos los comandos descriptos en este documento pueden ejecutarse de manera indistinta a través de cualquiera de los siguientes canales de comunicación:

1. **Interfaz Serial Local (UART0 — USB Debug):**
    * **Configuración:** `115200 bps` (`8N1`), terminador de línea `\n` o `\r\n`.
    * **Respuesta:** Impresa de forma inmediata por la misma consola serie.
2. **Broker MQTT Remoto (Tópico de Comandos):**
    * **Tópico de Suscripción (Entrada):** `DVL/LILY-GO/<ID>/COMANDOS` (ej. `DVL/LILY-GO/60000/COMANDOS`).
    * **Tópico de Publicación (Respuesta):** `DVL/LILY-GO/<ID>/RESPUESTA` (ej. `DVL/LILY-GO/60000/RESPUESTA`).
3. **Retransmisión a Satélites (Modo Gateway AP Bridge / Serial Bridge):**
    * Cuando el equipo opera como Hub Central (`DVL+EN_WIFI=2` o `DVL+EN_SERIAL=2`), se suscribe a los tópicos comodín `DVL/+/+/COMANDOS` y `DVL/+/+/FOTA`, encolando y entregando automáticamente los comandos dirigidos a los nodos satélites asociados.

---

## 2. Comandos de Sistema, Identificación y FOTA

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+ID=<id>` | Define el identificador único del equipo en Flash NVM, actualiza el tópico raíz a `DVL/LILY-GO/<id>` y fuerza la reconexión al Broker MQTT. | `DVL+ID=60001` | `Rident_OK (TOPIC: DVL/LILY-GO/60001)` |
| `DVL+VER` | Devuelve la versión de firmware compilada actualmente en ejecución. | `DVL+VER` | `VERSION=V07.18.00` |
| `DVL+RESET` | Ejecuta un reinicio inmediato por software del microcontrolador ESP32 (`ESP.restart()`). | `DVL+RESET` | `>> Reiniciando dispositivo...` |
| `DVL+STIME=<seg>` | Configura y persiste en Flash el intervalo periódico de publicación de telemetría de sensores (en segundos). | `DVL+STIME=60` | `TIEMPO SETEADO OK` |
| `DVL+QTIME` | Consulta el intervalo de publicación de telemetría configurado actualmente. | `DVL+QTIME` | `TIME=60` |
| `DVL+FOTA=<url>` | Inicia una actualización remota de firmware (FOTA) descargando el binario desde la URL indicada (soporta WiFi o GPRS según la interfaz activa). | `DVL+FOTA=http://servidor.com/fw/iot_lite_v071800.bin` | `FOTA_INICIADA` |
| `DVL+CFG_FOTA=<t>,<sec>` | Configura los parámetros de descarga FOTA en NVM:<br>• `<t>`: Timeout máximo en segundos (`> 0`).<br>• `<sec>`: Protocolo (`0` = HTTP estándar, `1` = HTTPS seguro). | `DVL+CFG_FOTA=600,0` | `>> FOTA CONFIGURADO: TIMEOUT=600s, SEGURIDAD=0` |
| `DVL+QCFG_FOTA` | Consulta los parámetros actuales de timeout y seguridad configurados para FOTA. | `DVL+QCFG_FOTA` | `TIMEOUT=600 \| SECURE=0` |

---

## 3. Conectividad Celular (Módem SIM7000G) y Diagnóstico Avanzado

El subsistema celular cuenta con selección inteligente de tecnología de acceso por radio (RAT), pre-configuración automática de contexto PDP (`AT+CGDCONT`), ventana de escaneo de **45 segundos** por tecnología con sondeo no bloqueante cada **3 segundos**, y niveles de diagnóstico en tiempo real.

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_MODEM=<1\|0>` | Habilita (`1`) o deshabilita (`0`) el módem celular SIM7000G. Al habilitarse enciende físicamente el módem y reactiva la máquina de estados celular; al deshabilitarse fuerza operación exclusiva por WiFi. | `DVL+EN_MODEM=1` | `>> HABILITADO MODEM` |
| `DVL+QEN_MODEM` | Consulta si el módem celular se encuentra habilitado en configuración. | `DVL+QEN_MODEM` | `EN_MODEM=1` |
| `DVL+SAPN=<apn>` | Configura y almacena en memoria Flash el APN de la operadora celular. | `DVL+SAPN=igprs.claro.com.ar` | `APN SETEADO OK` |
| `DVL+QAPN` | Consulta el APN configurado actualmente en memoria no volátil. | `DVL+QAPN` | `APN=igprs.claro.com.ar` |
| `DVL+MODEM_TECH=<modo>` | Configura y persiste la política de selección de tecnología celular, aplicando el cambio en el acto:<br>• `0`: **Rotación Automática** (`LTE CAT-M1` → `LTE NB-IoT` → `2G GSM/GPRS`, 45s por ventana).<br>• `1`: **Fijo LTE CAT-M1 (eMTC)** (`AT+CNMP=38`, `AT+CMNB=1`).<br>• `2`: **Fijo LTE NB-IoT** (`AT+CNMP=38`, `AT+CMNB=2`).<br>• `3`: **Fijo 2G GSM/GPRS/EDGE** (`AT+CNMP=13`). | `DVL+MODEM_TECH=0` | `>> MODEM_TECH=0 (AUTO (CAT-M1 -> NB-IoT -> 2G))` |
| `DVL+QMODEM_TECH` | Consulta la política de tecnología configurada y la tecnología efectivamente activa en la celda actual (`+CPSI?`). | `DVL+QMODEM_TECH` | `MODEM_TECH=0 (AUTO (CAT-M1 -> NB-IoT -> 2G)) \| ACTIVA=2G (GSM/GPRS/EDGE)` |
| `DVL+DBG_MODEM=<nivel>` | Configura y persiste en NVM el nivel de verborragia de diagnóstico del módem por consola serial:<br>• `0`: **Normal / Resumido** (solo eventos principales).<br>• `1`: **Diagnóstico Detallado** (estado SIM `+CPIN`, temporizador de ventana `Xs/45s`, decodificación de registro `+CREG`/`+CGREG`/`+CEREG`, señal `+CSQ` en dBm y celda `+CPSI`).<br>• `2`: **Verborrágico Total + Raw AT** (incluye Nivel 1 + intercepción en vivo de cada comando enviado `[AT >>]` y respuesta recibida `[AT <<]` con timestamp en `ms`). | `DVL+DBG_MODEM=1` | `>> DBG_MODEM=1 (DIAGNOSTICO DETALLADO DE ESTADOS Y SENAL)` |
| `DVL+QDBG_MODEM` | Consulta el nivel de verborragia/diagnóstico del módem activo. | `DVL+QDBG_MODEM` | `DBG_MODEM=1` |
| `DVL+MODEM_STATUS` | Ejecuta una auditoría instantánea del módem devolviendo el estado físico de UART AT, tarjeta SIM, modo configurado, tecnología activa, APN, nivel de señal (`CSQ` y dBm), parámetros de radiobase (`CPSI`), estados `CREG`/`CGREG`/`CEREG`, sesión de datos GPRS y dirección IP asignada. | `DVL+MODEM_STATUS` | *(Ver bloque de ejemplo debajo)* |

### Ejemplo de salida del comando `DVL+MODEM_STATUS`

```text
========== ESTADO DETALLADO DEL MODEM ==========
Estado Fisico UART : OK (115200 bps)
Nivel Debug Modem  : 1
Modo Configurado   : AUTO (CAT-M1 -> NB-IoT -> 2G)
Tecnologia Activa  : 2G (GSM/GPRS/EDGE)
Tarjeta SIM (+CPIN): 1 (READY)
APN Configurado    : igprs.claro.com.ar
Senal (+CSQ)       : 12 (-89 dBm)
Info Celda (+CPSI) : GSM,Online,722-310,0x0c45,23059,251 CELL 850,-91,0,12-12
Registro GSM CREG  : 1 (Registrado - Red Local/Home)
Registro 2G CGREG  : 1 (Registrado - Red Local/Home)
Registro LTE CEREG : 0 (No registrado, sin buscar)
Red Celular        : CONECTADA
Sesion Datos GPRS  : ACTIVA
IP Asignada        : 10.124.88.14
================================================
```

---

## 4. Conectividad WiFi y Topología Gateway AP Bridge

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_WIFI=<modo>` | Configura en caliente el modo de operación de la interfaz WiFi:<br>• `0`: **Deshabilitado** (apaga radio WiFi; el equipo transmite exclusivamente por módem celular).<br>• `1`: **Cliente Estándar (STA)** (conecta a la red WiFi configurada con fallback automático a GPRS).<br>• `2`: **Gateway AP Bridge** (levanta punto de acceso oculto local para concentrar satélites NodeMCU y salir a Internet por módem celular). | `DVL+EN_WIFI=2` | `>> HABILITADO WIFI AP BRIDGE` |
| `DVL+QEN_WIFI` | Consulta el modo de operación WiFi configurado actualmente (`0`, `1` o `2`). | `DVL+QEN_WIFI` | `EN_WIFI=2` |

### Parámetros Técnicos de la Red Local AP Bridge (`DVL+EN_WIFI=2`)
* **SSID:** `LILYGO_BRIDGE_NET` *(Red oculta / Hidden SSID)*
* **Seguridad / Contraseña (WPA2-PSK):** `TCSA-Bridge-2026`
* **Canal RF / Capacidad:** Canal `1` — Hasta **10 nodos satélites concurrentes**
* **Dirección IP del Gateway (IoT Lite):** `192.168.4.1` (Puerto HTTP `8080`)
* **Endpoints del Servidor Local:**
    * `POST /retransmit`: Recibe payloads JSON de los satélites, los publica en MQTT (o almacena en Flash ante corte) y responde con comandos encolados (`|CMD:<comando>`).
    * `GET /time`: Expone el epoch UTC actual para sincronización de RTC de los satélites.
    * `GET /fota_proxy`: Proxy HTTP de streaming que descarga y retransmite actualizaciones de firmware hacia los satélites a través del enlace celular.

---

## 5. Geolocalización (GNSS / Posicionamiento Manual)

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_GPS=<1\|0>` | Habilita (`1`) o deshabilita (`0`) la adquisición de coordenadas GNSS al inicio. Si se deshabilita (`0`), el módem inicia directamente la conexión celular sin demoras de búsqueda satelital. | `DVL+EN_GPS=0` | `>> DESHABILITADO GPS` |
| `DVL+QEN_GPS` | Consulta si la adquisición GNSS se encuentra habilitada. | `DVL+QEN_GPS` | `EN_GPS=0` |
| `DVL+SLAT=<lat>` | Define manualmente la latitud del punto de instalación en grados decimales y la persiste en Flash. | `DVL+SLAT=-34.603722` | `RLAT_OK` |
| `DVL+QLAT` | Consulta la última latitud válida almacenada (manual o adquirida por GNSS). | `DVL+QLAT` | `LAT=-34.603722` |
| `DVL+SLONG=<long>` | Define manualmente la longitud del punto de instalación en grados decimales y la persiste en Flash. | `DVL+SLONG=-58.381592` | `RLONG_OK` |
| `DVL+QLONG` | Consulta la última longitud válida almacenada (manual o adquirida por GNSS). | `DVL+QLONG` | `LONG=-58.381592` |

---

## 6. Sensores, Escáner BLE, Puerto Serial Secundario y Expansora

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_SENSOR=<1\|0>` | Habilita (`1`) o deshabilita (`0`) la lectura periódica del sensor de temperatura OneWire (`DS18B20`), publicado bajo el subtópico `/DS18B20`. | `DVL+EN_SENSOR=1` | `>> HABILITADO LECTURA SENSOR` |
| `DVL+QEN_SENSOR` | Consulta el estado de habilitación del sensor DS18B20. | `DVL+QEN_SENSOR` | `EN_SENSOR=1` |
| `DVL+EN_BLE=<1\|0>` | Habilita (`1`) o deshabilita (`0`) el escaneo pasivo BLE para sensores **MOKO** (compatible con filtros de nombre `DVL` y `PaPeR`). Al activarse (`1`), desactiva automáticamente `EN_SENSOR`, `EN_SERIAL` y `EN_MODBUS`. Publica en `/BLE/<MAC>`. | `DVL+EN_BLE=1` | `>> HABILITADO BLE` |
| `DVL+QEN_BLE` | Consulta si el escáner BLE se encuentra activo. | `DVL+QEN_BLE` | `EN_BLE=1` |
| `DVL+EN_SERIAL=<modo>` | Configura el modo del puerto serie secundario `UART2` (`RX=GPIO32`, `TX=GPIO33`). Al activarse (`1` o `2`), deshabilita `EN_MODBUS` por exclusión mutua:<br>• `0`: Deshabilitado.<br>• `1`: Lectura de placa expansora (`S0..S15`), publicada en `/SERIAL`.<br>• `2`: **Bridge Serial Bidireccional** (recibe JSONs de un nodo satélite por UART2, los retransmite a MQTT y devuelve hora epoch + comandos pendientes). | `DVL+EN_SERIAL=1` | `>> HABILITADO LECTURA SERIAL (EN_MODBUS=0)` |
| `DVL+QEN_SERIAL` | Consulta el modo configurado en el puerto serial secundario (`0`, `1` o `2`). | `DVL+QEN_SERIAL` | `EN_SERIAL=1` |
| `DVL+SBAUD=<baud>` | Configura en tiempo real y persiste en NVM la velocidad de `UART2`. Valores admitidos: `1200`, `2400`, `4800`, `9600`, `19200`, `38400`, `57600`, `115200`. | `DVL+SBAUD=9600` | `>> BAUD RATE SETEADO A 9600` |
| `DVL+QBAUD` | Consulta el baud rate actual configurado para `UART2`. | `DVL+QBAUD` | `BAUD=9600` |
| `DVL+SFINI=<texto>` | Configura la cadena marcadora de inicio de trama para el parser serial en modo `1`. | `DVL+SFINI=DATA,` | `[OK] StartMarker: DATA,` |
| `DVL+QFINI` | Consulta el marcador de inicio de trama configurado. | `DVL+QFINI` | `START_MARKER=DATA,` |
| `DVL+SFFIN=<texto>` | Configura la cadena marcadora de fin de trama para el parser serial en modo `1`. | `DVL+SFFIN=\r` | `[OK] EndMarker: \r` |
| `DVL+QFFIN` | Consulta el marcador de fin de trama configurado. | `DVL+QFFIN` | `END_MARKER=\r` |
| `DVL+SPARSE=<char>` | Configura el carácter delimitador de campos del parser serial. | `DVL+SPARSE=,` | `[OK] Separador: ,` |
| `DVL+QPARSE` | Consulta el carácter separador del parser serial. | `DVL+QPARSE` | `SEPARATOR=,` |
| `DVL+EXP_RESET` | Realiza un ciclo de potencia física (apaga durante 1s y vuelve a energizar `SENSOR_POWER_PIN`) sobre la placa expansora externa. | `DVL+EXP_RESET` | `>> Reiniciando expansora...` |
| `EXP+<comando>` | Reenvía en forma transparente la instrucción `EXP+<comando>` por `UART2` hacia la placa expansora y aguarda hasta 5 segundos su respuesta. | `EXP+STATUS` | `RESP_EXPANSORA: OK` |

---

## 7. Maestro Modbus RTU (Sobre UART2)

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_MODBUS=<1\|0>` | Habilita (`1`) o deshabilita (`0`) el motor de consultas Modbus RTU sobre `UART2`. Al habilitarse, desactiva automáticamente `EN_SERIAL` (`0`). Publica lecturas bajo el subtópico `/MODBUS`. | `DVL+EN_MODBUS=1` | `>> HABILITADO MODBUS (EN_SERIAL=0)` |
| `DVL+QEN_MODBUS` | Consulta si el módulo Modbus RTU está habilitado. | `DVL+QEN_MODBUS` | `EN_MODBUS=1` |
| `DVL+MODBUS=<idx>,<id>,<fn>,<addr>,<qty>` | Programa y guarda en NVM una de las 5 ranuras de consulta cíclica Modbus (admite valores decimales o hexadecimales `0xNN`):<br>• `<idx>`: Ranura de consulta (`1` a `5`).<br>• `<id>`: Dirección del esclavo Modbus (`1..247`).<br>• `<fn>`: Código de función (`3` = Holding Registers, `4` = Input Registers).<br>• `<addr>`: Dirección inicial de registro.<br>• `<qty>`: Cantidad de registros de 16 bits a leer. | `DVL+MODBUS=1,10,3,0x00,2` | `[OK] MODBUS#1 ID=0xa FUNC=0x3 ADDR=0x0 QTY=0x2` |
| `DVL+MODBUSCLR=<idx\|ALL>` | Borra una ranura de consulta Modbus específica (`1..5`) o limpia las 5 ranuras (`ALL`). | `DVL+MODBUSCLR=ALL` | `[OK] Todas las tramas Modbus borradas` |
| `DVL+QMODBUS` | Imprime por puerto serie la tabla detallada de las 5 configuraciones de tramas Modbus y sus últimos valores leídos. | `DVL+QMODBUS` | `[OK] Ver detalle por Serial` |

---

## 8. Conversor Analógico-Digital (ADC) y Monitoreo de Alimentación

### Asignación de Pines Analógicos (ESP32 ADC1 — 12 bits, Atenuación 11 dB)
* **`GPIO36` (`S_VP / SOLAR_IN`) — Alimentación Principal (`Tension_principal`):** Muestreo automático con promedio de 8 lecturas (`analogReadMilliVolts`) y compensación de divisor resistivo (`x2.0`). Se reporta **siempre de forma nativa** en el JSON de `keep-alive` y en las tramas de sensores (`DS18B20`, `SERIAL`, `MODBUS`, `BLE`).
* **`GPIO35` (`IO35 / VVBAT`) — Alimentación de Backup / Batería (`Tension_backup`):** Muestreo automático con promedio de 8 lecturas (`analogReadMilliVolts`) y compensación de divisor resistivo (`x2.0`). Se reporta **siempre de forma nativa** en el JSON de `keep-alive` y en las tramas de sensores.
* **`GPIO34` — Entrada Analógica Externa `ADC0` (Índice `<n> = 0`):** Canal analógico configurable con filtro de umbral mínimo, promedio móvil (`cantMed`) y calibración lineal (`factor` / `offset`). Publicado bajo la clave `"adc0"` en el subtópico `/ADC`.
* **`GPIO39` (`S_VN`) — Entrada Analógica Externa `ADC1` (Índice `<n> = 1`):** Canal analógico configurable con filtro de umbral mínimo, promedio móvil (`cantMed`) y calibración lineal (`factor` / `offset`). Publicado bajo la clave `"adc1"` en el subtópico `/ADC`.

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_ADC=<1\|0>` | Habilita (`1`) o deshabilita (`0`) la publicación periódica independiente de **`ADC0` (`GPIO34`)** y **`ADC1` (`GPIO39`)** en el subtópico `/ADC`. | `DVL+EN_ADC=1` | `>> HABILITADO REPORTE ADC INDEPENDIENTE` |
| `DVL+QEN_ADC` | Consulta si el reporte independiente `/ADC` se encuentra habilitado. | `DVL+QEN_ADC` | `EN_ADC=1` |
| `DVL+SFIL=<n>,<m>,<p>` | Configura los parámetros de umbral/filtro para el canal ADC `<n>` (**`0` = `GPIO34 / ADC0`**, **`1` = `GPIO39 / ADC1`**), persistidos en `adc_config`:<br>• `<m>`: Umbral mínimo tolerable (si la lectura es `<= m`, se reemplaza por `<p>`).<br>• `<p>`: Valor de reemplazo asignado. | `DVL+SFIL=0,0.5,10.0` | `FILTRO SETEADO OK` |
| `DVL+QFIL` | Consulta los coeficientes de filtro configurados para ambos canales ADC (**`FIL0` = `GPIO34`**, **`FIL1` = `GPIO39`**). | `DVL+QFIL` | `FIL0=0.50,10.00 \| FIL1=0.00,0.00` |
| `DVL+SFACTOR=<n>,<m>,<p>` | Configura el factor de escala lineal (`m`, ganancia) y desplazamiento (`p`, offset) para calibrar el canal ADC `<n>` (**`0` = `GPIO34 / ADC0`**, **`1` = `GPIO39 / ADC1`**): `Valor = (Lectura * m) + p`. | `DVL+SFACTOR=0,1.0,0.0` | `FACTOR SETEADO OK` |
| `DVL+QFACTOR` | Consulta los factores de ganancia y offset de ambos canales ADC (**`FAC0` = `GPIO34`**, **`FAC1` = `GPIO39`**). | `DVL+QFACTOR` | `FAC0=1.00,0.00 \| FAC1=1.00,0.00` |
| `DVL+SALI=<muestras>` | Define la cantidad de muestras utilizadas para el filtro de alisado (promedio móvil) de las entradas `GPIO34` (`ADC0`) y `GPIO39` (`ADC1`) (`> 0`, por defecto `50`). | `DVL+SALI=50` | `ALISADO SETEADO OK` |
| `DVL+QALI` | Consulta la cantidad de muestras de alisado configurada actualmente. | `DVL+QALI` | `ALISADO=50` |
| `DVL+STADC=<ciclos>` | Define el período de muestreo de `GPIO34` y `GPIO39` en múltiplos de `100 ms` (rango válido: `1` a `65000`, ej. `10` = `1000 ms`). | `DVL+STADC=10` | `TIME ADC SETEADO OK` |
| `DVL+QTADC` | Consulta el divisor de tiempo de muestreo ADC configurado. | `DVL+QTADC` | `TIME ADC=10` |
