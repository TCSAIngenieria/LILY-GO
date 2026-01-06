# Comandos de Configuración LILY-GO

Este documento detalla todos los comandos disponibles para configurar y controlar el dispositivo LILY-GO a través de la interfaz serial o MQTT.

## Comandos de Configuración de Sistema

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+RESET` | Reinicia el dispositivo ESP32. | `DVL+RESET` |
| `DVL+ID=<id>` | Establece el Identificador del dispositivo. | `DVL+ID=OBU_123` |
| `DVL+VER` | Devuelve la versión actual del firmware. | `DVL+VER` |
| `DVL+FOTA=<url>` | Inicia una actualización de firmware desde la URL especificada. | `DVL+FOTA=http://midominio.com/firmware.bin` |

## Comandos de Configuración de Sensores y Módulos

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+EN_SENSOR` | Habilita la lectura del sensor conectado (ej. DS18B20). | `DVL+EN_SENSOR` |
| `DVL+EN_SERIAL` | Habilita la lectura del puerto serial secundario. | `DVL+EN_SERIAL` |
| `DVL+EN_MODBUS=<1\|0>` | Habilita (1) o deshabilita (0) el módulo Modbus. Al habilitar Modbus, se deshabilita Serial. | `DVL+EN_MODBUS=1` |
| `DVL+EN_BLE` | Habilita el escaneo de sensores BLE (deshabilita otros sensores). | `DVL+EN_BLE` |
| `DVL+EXP_RESET` | Reinicia la placa expansora (ciclo de energía). | `DVL+EXP_RESET` |

## Comandos de Modbus

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+MODBUS=<idx>,<ID>,<FUNC>,<ADDR>,<QTY>` | Configura una trama Modbus en el índice `idx` (1-5). | `DVL+MODBUS=1,10,3,0,2`<br>(Lee 2 registros desde addr 0 del slave 10) |
| `DVL+MODBUSCLR=<idx\|ALL>` | Borra una trama específica (`idx`) o todas (`ALL`). | `DVL+MODBUSCLR=1` o `DVL+MODBUSCLR=ALL` |

## Comandos de ADC (Conversor Analógico-Digital)

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+SFIL=<n>,<m>,<p>` | Configura el filtro para el canal ADC `n` (0-2). | `DVL+SFIL=0,0.5,10` |
| `DVL+SFACTOR=<n>,<m>,<p>` | Configura el factor (m) y offset (p) para el canal ADC `n` (0-2). | `DVL+SFACTOR=0,1.0,0.0` |
| `DVL+SALI=<val>` | Establece la cantidad de mediciones para el alisado (promedio). | `DVL+SALI=50` |
| `DVL+STADC=<val>` | Establece el tiempo de muestreo ADC en ms (1-65000). | `DVL+STADC=100` |

## Comandos de Geolocalización (Manual)

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+SLAT=<lat>` | Establece la latitud manualmente (si no hay GPS). | `DVL+SLAT=-34.6037` |
| `DVL+SLONG=<long>` | Establece la longitud manualmente (si no hay GPS). | `DVL+SLONG=-58.3816` |

## Comandos de Parser y Formato de Datos

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+SFINI=<txt>` | Establece el marcador de inicio de trama. | `DVL+SFINI=DATA,` |
| `DVL+SFFIN=<txt>` | Establece el marcador de fin de trama. | `DVL+SFFIN=\r` |
| `DVL+SPARSE=<char>` | Establece el caracter separador de datos. | `DVL+SPARSE=,` |
| `DVL+STIME=<seg>` | Establece el intervalo de publicación de datos en segundos. | `DVL+STIME=60` |
| `EXP+<cmd>` | Envía el comando `<cmd>` directamente a la expansora por serial secundario. | `EXP+LEER` |

## Comandos de Consulta (Query)

| Comando | Descripción | Retorno Ejemplo |
|:---|:---|:---|
| `DVL+QEN_SENSOR` | Consulta si el sensor está habilitado. | `EN_SENSOR=1` |
| `DVL+QEN_SERIAL` | Consulta si el serial está habilitado. | `EN_SERIAL=0` |
| `DVL+QEN_MODBUS` | Consulta si Modbus está habilitado. | `EN_MODBUS=1` |
| `DVL+QEN_BLE` | Consulta si BLE está habilitado. | `EN_BLE=0` |
| `DVL+QMODBUS` | Muestra las tramas Modbus configuradas (salida por Serial). | `[OK] Ver detalle por Serial` |
| `DVL+QFIL` | Muestra los coeficientes de filtro ADC. | `FIL0=0.1,0.0 ...` |
| `DVL+QFACTOR` | Muestra los factores de corrección ADC. | `FAC0=1.0,0.0 ...` |
| `DVL+QTADC` | Muestra el tiempo de muestreo ADC. | `TIME ADC=100` |
| `DVL+QALI` | Muestra la cantidad de muestras para alisado. | `ALISADO=50` |
| `DVL+QTIME` | Muestra el intervalo de reporte actual. | `TIME=60` |
| `DVL+QFINI` | Muestra el marcador de inicio actual. | `START_MARKER=DATA,` |
| `DVL+QFFIN` | Muestra el marcador de fin actual. | `END_MARKER=\r` |
| `DVL+QPARSE` | Muestra el separador configurado. | `SEPARATOR=,` |
| `DVL+QLAT` | Muestra la última latitud registrada. | `LAT=-34.60` |
| `DVL+QLONG` | Muestra la última longitud registrada. | `LONG=-58.38` |
