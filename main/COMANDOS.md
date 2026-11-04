# Manual Técnico de Comandos CLI / MQTT — IoT Lite (AGD Pivots)

## 1. Arquitectura de Transporte y Sintaxis General

Todos los comandos descriptos en este documento pueden ejecutarse de manera indistinta a través de cualquiera de los siguientes canales de comunicación:

1. **Interfaz Serial Local (UART0 — USB Debug):**
    * **Configuración:** `115200 bps` (`8N1`), terminador de línea `\n` o `\r\n`.
    * **Respuesta:** Impresa de forma inmediata por la misma consola serie.
2. **Broker MQTT Remoto (Tópico de Comandos):**
    * **Tópico de Suscripción (Entrada):** `DVL/LILY-GO/<ID>/COMANDOS` (ej. `DVL/LILY-GO/60000/COMANDOS`).
    * **Tópico de Publicación (Respuesta):** `DVL/LILY-GO/<ID>/RESPUESTA` (ej. `DVL/LILY-GO/60000/RESPUESTA`).
3. **Tópicos de Telemetría y Estado:**
    * **Reporte PIVOT (Entradas Digitales, Sirena y Alimentación):** `DVL/LILY-GO/<ID>/PIVOT` (`DI1`, `DI2`, `Sirena`, `PivotEnabled`, `Tension_backup`, `Tension_principal`, `index`).
    * **Reporte Analógico (ADC):** `DVL/LILY-GO/<ID>/ADC` (`adc0`, `adc1`).
    * **Reporte Keep-Alive / Diagnóstico de Red:** `DVL/LILY-GO/<ID>` (`status: "keep-alive"`, `Tension_principal`, `Tension_backup`, `conn_type`, `conn_detail`, `IMEI`, `IMSI`, `ICCID`, `RSRQ`, `RSRP`, `RSSI`).

---

## 2. Comandos de Sistema, Identificación y FOTA

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+ID=<id>` | Define el identificador único del equipo en Flash NVM, actualiza el tópico raíz a `DVL/LILY-GO/<id>` y fuerza la reconexión al Broker MQTT. | `DVL+ID=60001` | `Rident_OK (TOPIC: DVL/LILY-GO/60001)` |
| `DVL+VER` | Devuelve la versión de firmware compilada actualmente en ejecución. | `DVL+VER` | `VERSION=V03.01.01-AGD_Pivots` |
| `DVL+RESET` | Ejecuta un reinicio inmediato por software del microcontrolador ESP32 (`ESP.restart()`). | `DVL+RESET` | `>> Reiniciando dispositivo...` |
| `DVL+STIME=<seg>` | Configura y persiste en Flash el intervalo periódico de publicación de telemetría (en segundos, por defecto `300`). | `DVL+STIME=300` | `TIEMPO SETEADO OK` |
| `DVL+QTIME` | Consulta el intervalo de publicación de telemetría configurado actualmente. | `DVL+QTIME` | `TIME=300` |
| `DVL+FOTA=<url>` | Inicia una actualización remota de firmware (FOTA) descargando el binario desde la URL indicada (soporta WiFi o GPRS según la interfaz activa). | `DVL+FOTA=http://servidor.com/fw/iot_lite_pivot_v030101.bin` | `FOTA_INICIADA` |
| `DVL+CFG_FOTA=<t>,<sec>` | Configura los parámetros de descarga FOTA en NVM:<br>• `<t>`: Timeout máximo en segundos (`> 0`).<br>• `<sec>`: Protocolo (`0` = HTTP estándar, `1` = HTTPS seguro). | `DVL+CFG_FOTA=600,0` | `>> FOTA CONFIGURADO: TIMEOUT=600s, SEGURIDAD=0` |
| `DVL+QCFG_FOTA` | Consulta los parámetros actuales de timeout y seguridad configurados para FOTA. | `DVL+QCFG_FOTA` | `TIMEOUT=600 \| SECURE=0` |

---

## 3. Conectividad Celular (Módem SIM7000G), WiFi y Diagnóstico Avanzado

El subsistema celular cuenta con selección inteligente de tecnología de acceso por radio (RAT), pre-configuración automática de contexto PDP (`AT+CGDCONT`), ventana de escaneo de **45 segundos** por tecnología con sondeo no bloqueante cada **3 segundos**, estados cacheados en `loop()` y niveles de diagnóstico en tiempo real.

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_MODEM=<1\|0>` | Habilita (`1`) o deshabilita (`0`) el módem celular SIM7000G. Al habilitarse verifica respuesta `AT` y enciende el módem; al deshabilitarse fuerza operación exclusiva por WiFi. | `DVL+EN_MODEM=1` | `>> HABILITADO MODEM` |
| `DVL+QEN_MODEM` | Consulta si el módem celular se encuentra habilitado en configuración. | `DVL+QEN_MODEM` | `EN_MODEM=1` |
| `DVL+SAPN=<apn>` | Configura y almacena en memoria Flash el APN de la operadora celular y pre-configura el perfil PDP (`AT+CGDCONT`). | `DVL+SAPN=igprs.claro.com.ar` | `APN SETEADO OK` |
| `DVL+QAPN` | Consulta el APN configurado actualmente en memoria no volátil. | `DVL+QAPN` | `APN=igprs.claro.com.ar` |
| `DVL+MODEM_TECH=<modo>` | Configura y persiste la política de selección de tecnología celular, aplicando el cambio en el acto:<br>• `0`: **Rotación Automática** (`LTE CAT-M1` → `LTE NB-IoT` → `2G GSM/GPRS`, 45s por ventana).<br>• `1`: **Fijo LTE CAT-M1 (eMTC)** (`AT+CNMP=38`, `AT+CMNB=1`).<br>• `2`: **Fijo LTE NB-IoT** (`AT+CNMP=38`, `AT+CMNB=2`).<br>• `3`: **Fijo 2G GSM/GPRS/EDGE** (`AT+CNMP=13`). | `DVL+MODEM_TECH=0` | `>> MODEM_TECH=0 (AUTO (CAT-M1 -> NB-IoT -> 2G))` |
| `DVL+QMODEM_TECH` | Consulta la política de tecnología configurada y la tecnología efectivamente activa en la celda actual (`+CPSI?`). | `DVL+QMODEM_TECH` | `MODEM_TECH=0 (AUTO (CAT-M1 -> NB-IoT -> 2G)) \| ACTIVA=2G (GSM/GPRS/EDGE)` |
| `DVL+DBG_MODEM=<nivel>` | Configura y persiste en NVM el nivel de verborragia de diagnóstico del módem por consola serial:<br>• `0`: **Normal / Resumido** (solo eventos principales).<br>• `1`: **Diagnóstico Detallado** (estado SIM `+CPIN`, temporizador de ventana `Xs/45s`, decodificación de registro `+CREG`/`+CGREG`/`+CEREG`, señal `+CSQ` en dBm y celda `+CPSI`).<br>• `2`: **Verborrágico Total + Raw AT** (incluye Nivel 1 + intercepción en vivo de cada comando enviado `[AT >>]` y respuesta recibida `[AT <<]` con timestamp en `ms`). | `DVL+DBG_MODEM=1` | `>> DBG_MODEM=1 (DIAGNOSTICO DETALLADO DE ESTADOS Y SENAL)` |
| `DVL+QDBG_MODEM` | Consulta el nivel de verborragia/diagnóstico del módem activo. | `DVL+QDBG_MODEM` | `DBG_MODEM=1` |
| `DVL+MODEM_STATUS` | Ejecuta una auditoría instantánea del módem devolviendo el estado físico de UART AT, tarjeta SIM, modo configurado, tecnología activa, APN, nivel de señal (`CSQ` y dBm), parámetros de radiobase (`CPSI`), estados `CREG`/`CGREG`/`CEREG`, sesión de datos GPRS y dirección IP asignada. | `DVL+MODEM_STATUS` | *(Ver bloque de ejemplo debajo)* |
| `DVL+PASSON` | Activa el modo transparente (**Passthrough**), desactivando el Watchdog y puenteando `UART0` ↔ `UART1` para enviar comandos AT manuales al módem. | `DVL+PASSON` | `MODO PASSTHROUGH ACTIVADO` |
| `DVL+PASSOFF` | Comando enviado dentro del modo transparente para finalizar la sesión Passthrough y reiniciar el equipo. | `DVL+PASSOFF` | `[PASSTHROUGH] Desactivando y reiniciando...` |
| `DVL+EN_WIFI=<1\|0>` | Habilita (`1`, modo Cliente STA) o deshabilita (`0`, operación exclusiva por módem celular) la interfaz WiFi. | `DVL+EN_WIFI=1` | `>> HABILITADO WIFI CLIENTE (INTENTANDO CONEXION)` |
| `DVL+QEN_WIFI` | Consulta si la interfaz WiFi se encuentra habilitada (`1`) o deshabilitada (`0`). | `DVL+QEN_WIFI` | `EN_WIFI=1` |

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

## 4. Sistema PIVOT (Monitoreo de Entradas Digitales, Sirena y Deep Sleep)

### Asignación de Pines Digitales y Lógica Operativa

* **`GPIO13` (`PIN_IN_1` — Entrada Digital `DI1`):** Configurada como `INPUT_PULLUP` con filtro antirrebote por software de **3 segundos** y capacidad de despertar al microcontrolador desde **Deep Sleep** (`EXT1_WAKEUP_ANY_HIGH`).
* **`GPIO14` (`PIN_IN_2` — Entrada Digital `DI2`):** Configurada como `INPUT_PULLUP` con filtro antirrebote por software de **3 segundos** y capacidad de despertar al microcontrolador desde **Deep Sleep** (`EXT1_WAKEUP_ANY_HIGH`).
* **`GPIO15` (`PIN_SIREN` — Salida de Sirena):** Salida digital activa en alto (`HIGH`). Se dispara cuando el sistema está habilitado (`en_pivot = 1`) y ambas entradas estables se encuentran en `HIGH` (`DI1 == 1 && DI2 == 1`) durante un tiempo mayor o igual a `delay_sirena`, quedando enclavada hasta recibir `DVL+PIVOFF` o cumplir `siren_duration`.

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+PIVON` | Habilita el sistema de monitoreo y alarma PIVOT. Verifica previamente que `PIN_IN_1` (`GPIO13`) y `PIN_IN_2` (`GPIO14`) tengan estados lógicos distintos; si son iguales rechaza la activación indicando revisar la instalación. | `DVL+PIVON` | `>> SISTEMA PIVOT HABILITADO` |
| `DVL+PIVOFF` | Deshabilita el sistema PIVOT (`en_pivot = 0`), apaga inmediatamente la salida `PIN_SIREN` (`GPIO15`) y libera el enclavamiento de alarma. | `DVL+PIVOFF` | `>> SISTEMA PIVOT DESHABILITADO (sirena apagada)` |
| `DVL+QPIV` | Consulta si el sistema PIVOT se encuentra habilitado (`1`) o deshabilitado (`0`). | `DVL+QPIV` | `PIVOT=1` |
| `DVL+SDSIR=<seg>` | Configura y persiste en NVM el tiempo de retardo (en segundos) desde la detección de condición de alarma hasta el encendido efectivo de la sirena (por defecto `300`). | `DVL+SDSIR=300` | `RETRASO SIRENA SETEADO OK (segundos): 300` |
| `DVL+QDSIR` | Consulta el retardo de activación de sirena configurado actualmente. | `DVL+QDSIR` | `DSIR=300` |
| `DVL+STSIR=<seg>` | Configura y persiste en NVM la duración máxima de sirena encendida en segundos (`0` = sin límite; al cumplirse el timeout apaga la sirena, auto-deshabilita `en_pivot = 0` y publica el evento en `/PIVOT`). | `DVL+STSIR=300` | `DURACION SIRENA SETEADO OK (segundos): 300` |
| `DVL+QTSIR` | Consulta el tiempo máximo de duración de sirena configurado. | `DVL+QTSIR` | `TSIR=300` |
| `DVL+SDEEP=<seg>` | Configura y persiste en NVM el tiempo de reposo en **Deep Sleep** en segundos (`0` = deshabilitado). Antes de dormir sincroniza RTC, publica `/PIVOT` + `keep-alive` y apaga MQTT, WiFi, módem, LED y sirena. | `DVL+SDEEP=300` | `DEEP SLEEP TIME SETEADO OK (segundos): 300` |
| `DVL+QDEEP` | Consulta el intervalo de Deep Sleep configurado actualmente. | `DVL+QDEEP` | `DEEP=300` |

---

## 5. Geolocalización (GNSS / Posicionamiento Manual)

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_GPS=<1\|0>` | Habilita (`1`) o deshabilita (`0`, valor por defecto en Pivot) la adquisición de coordenadas GNSS al inicio. Si está en `0`, el módem inicia directamente la conexión celular sin demora satelital. | `DVL+EN_GPS=0` | `>> DESHABILITADO GPS` |
| `DVL+QEN_GPS` | Consulta si la adquisición GNSS se encuentra habilitada. | `DVL+QEN_GPS` | `EN_GPS=0` |
| `DVL+SLAT=<lat>` | Define manualmente la latitud del punto de instalación en grados decimales y la persiste en Flash. | `DVL+SLAT=-34.603722` | `RLAT_OK` |
| `DVL+QLAT` | Consulta la última latitud válida almacenada (manual o adquirida por GNSS). | `DVL+QLAT` | `LAT=-34.603722` |
| `DVL+SLONG=<long>` | Define manualmente la longitud del punto de instalación en grados decimales y la persiste en Flash. | `DVL+SLONG=-58.381592` | `RLONG_OK` |
| `DVL+QLONG` | Consulta la última longitud válida almacenada (manual o adquirida por GNSS). | `DVL+QLONG` | `LONG=-58.381592` |

---

## 6. Conversor Analógico-Digital (ADC) y Monitoreo de Alimentación

### Asignación de Pines Analógicos (ESP32 ADC1 — 12 bits, Atenuación 11 dB)

El equipo utiliza **4 entradas analógicas** del periférico `ADC1` con atenuación de `11 dB`:

* **`GPIO36` (`S_VP / SOLAR_IN`) — Alimentación Principal (`Tension_principal`):** Muestreo automático con promedio de 8 lecturas (`analogReadMilliVolts`) y compensación de divisor resistivo (`x2.0`). Se reporta **siempre de forma nativa** en el JSON de `keep-alive` y en el reporte `/PIVOT`.
* **`GPIO35` (`IO35 / VVBAT`) — Alimentación de Backup / Batería (`Tension_backup`):** Muestreo automático con promedio de 8 lecturas (`analogReadMilliVolts`) y compensación de divisor resistivo (`x2.0`). Se reporta **siempre de forma nativa** en el JSON de `keep-alive` y en el reporte `/PIVOT`.
* **`GPIO34` (`ADC_PIN_34`) — Canal Analógico `ADC0` (Índice `<n> = 0`):** Entrada analógica general calibrada por `Vref` interno en **eFuse** (`esp_adc_cal`, promedio de 64 muestras) con filtro de umbral, promedio móvil (`cantMed`) y calibración lineal (`factor` / `offset`). Publicada como `"adc0"` en `/ADC` cuando `EN_ADC=1`.
* **`GPIO39` (`ADC_PIN_39`) — Canal Analógico `ADC1` (Índice `<n> = 1`):** Entrada analógica general calibrada por `Vref` interno en **eFuse** (`esp_adc_cal`, promedio de 64 muestras) con filtro de umbral, promedio móvil (`cantMed`) y calibración lineal (`factor` / `offset`). Publicada como `"adc1"` en `/ADC` cuando `EN_ADC=1`.

| Comando | Parámetros y Descripción Técnica | Ejemplo de Envío | Respuesta Esperada |
|:---|:---|:---|:---|
| `DVL+EN_ADC=<1\|0>` | Habilita (`1`) o deshabilita (`0`) la publicación periódica en el subtópico `/ADC` de los canales `ADC0` (`GPIO34`) y `ADC1` (`GPIO39`). | `DVL+EN_ADC=1` | `>> HABILITADO REPORTE ADC` |
| `DVL+QEN_ADC` | Consulta si el reporte `/ADC` se encuentra habilitado. | `DVL+QEN_ADC` | `EN_ADC=1` |
| `DVL+SFIL=<n>,<m>,<p>` | Configura los parámetros de umbral/filtro para el canal ADC `<n>` (**`0` = `GPIO34 / ADC0`**, **`1` = `GPIO39 / ADC1`**), persistidos en `adc_config`:<br>• `<m>`: Umbral mínimo tolerable (si la lectura es `<= m`, se reemplaza por `<p>`).<br>• `<p>`: Valor de reemplazo asignado. | `DVL+SFIL=0,0.5,0.0` | `FILTRO SETEADO OK` |
| `DVL+QFIL` | Consulta los coeficientes de filtro configurados para `ADC0` (`GPIO34`) y `ADC1` (`GPIO39`). | `DVL+QFIL` | `FIL0=0.50,0.00 \| FIL1=0.00,0.00` |
| `DVL+SFACTOR=<n>,<m>,<p>` | Configura el factor de escala lineal (`m`, ganancia) y desplazamiento (`p`, offset) para calibrar el canal ADC `<n>` (**`0` = `GPIO34 / ADC0`**, **`1` = `GPIO39 / ADC1`**): `Valor = (Lectura * m) + p`. | `DVL+SFACTOR=0,1.0,0.0` | `FACTOR SETEADO OK` |
| `DVL+QFACTOR` | Consulta los factores de ganancia y offset de `ADC0` (`GPIO34`) y `ADC1` (`GPIO39`). | `DVL+QFACTOR` | `FAC0=1.00,0.00 \| FAC1=1.00,0.00` |
| `DVL+SALI=<muestras>` | Define la cantidad de muestras utilizadas para el filtro de alisado (promedio móvil exponencial) de los canales `ADC0` y `ADC1` (`> 0`, por defecto `50`). | `DVL+SALI=50` | `ALISADO SETEADO OK` |
| `DVL+QALI` | Consulta la cantidad de muestras de alisado configurada actualmente. | `DVL+QALI` | `ALISADO=50` |
| `DVL+STADC=<ciclos>` | Define el período de muestreo de los canales ADC en múltiplos de `100 ms` (rango válido: `1` a `65000`, ej. `10` = `1000 ms`). | `DVL+STADC=10` | `TIME ADC SETEADO OK` |
| `DVL+QTADC` | Consulta el divisor de tiempo de muestreo ADC configurado. | `DVL+QTADC` | `TIME ADC=10` |
