# Comandos de Configuración LILY-GO

Este documento detalla todos los comandos disponibles para configurar y controlar el dispositivo LILY-GO a través de la interfaz serial o MQTT.

## Comandos de Configuración de Sistema

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+RESET` | Reinicia el dispositivo ESP32. | `DVL+RESET` |
| `DVL+ID=<id>` | Establece el Identificador del dispositivo. | `DVL+ID=OBU_123` |
| `DVL+VER` | Devuelve la versión actual del firmware. | `DVL+VER` |
| `DVL+FOTA=<url>` | Inicia una actualización de firmware desde la URL especificada. | `DVL+FOTA=http:// midominio.com/ firmware.bin` |
| `DVL+STIME=<seg>` | Establece el intervalo de publicación de datos en segundos. | `DVL+STIME=60` |
| `DVL+PASSON` | Habilita el modo Passthrough para comunicarse directo con el módem. | `DVL+PASSON` |

## Comandos de Módulos (ADC)

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+EN_ADC=<1\|0>` | Habilita (1) o deshabilita (0) el reporte de los valores del ADC. | `DVL+EN_ADC=1` |

## Comandos de Geolocalización (Manual)

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+SLAT=<lat>` | Establece la latitud manualmente (si no hay GPS). | `DVL+SLAT=-34.6037` |
| `DVL+SLONG=<long>` | Establece la longitud manualmente (si no hay GPS). | `DVL+SLONG=-58.3816` |

## Comandos de Sistema PIVOT (Monitoreo y Alarma)

| Comando | Descripción | Ejemplo |
|:---|:---|:---|
| `DVL+PIVON` | Habilita el sistema PIVOT (requiere entradas distintas). | `DVL+PIVON` |
| `DVL+PIVOFF` | Deshabilita el sistema, apaga sirena y limpia el enclavamiento. | `DVL+PIVOFF` |
| `DVL+SDSIR=<seg>` | Configura el retardo antes de activar la sirena (segundos). | `DVL+SDSIR=300` |
| `DVL+STSIR=<seg>` | Configura el tiempo máximo de sirena encendida (0=infinito). | `DVL+STSIR=60` |
| `DVL+SDEEP=<seg>` | Configura el intervalo de Deep Sleep (0=deshabilitado). | `DVL+SDEEP=300` |

## Comandos de Consulta (Query)

| Comando | Descripción | Retorno Ejemplo |
|:---|:---|:---|
| `DVL+QTIME` | Muestra el intervalo de reporte actual. | `TIME=60` |
| `DVL+QEN_ADC` | Consulta si el reporte ADC está habilitado. | `EN_ADC=1` |
| `DVL+QLAT` | Muestra la última latitud registrada. | `LAT=-34.60` |
| `DVL+QLONG` | Muestra la última longitud registrada. | `LONG=-58.38` |
| `DVL+QPIV` | Consulta si el sistema PIVOT está habilitado. | `PIVOT=1` |
| `DVL+QDSIR` | Consulta el retardo de activación de la sirena. | `DSIR=300` |
| `DVL+QTSIR` | Consulta el tiempo de duración de la sirena. | `TSIR=60` |
| `DVL+QDEEP` | Consulta el intervalo de Deep Sleep. | `DEEP=300` |