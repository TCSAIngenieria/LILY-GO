# Documento de Requisitos de Producto (PRD) - Nodo IoT NodeMCU

| Versión | Fecha | Autor | Descripción |
|:---|:---|:---|:---|
| 1.0 | 2026-06-11 | — | Versión inicial del PRD |

---

## 1. Resumen Ejecutivo

El **Nodo IoT NodeMCU** es un dispositivo de telemetría industrial basado en el microcontrolador ESP32 (placa NodeMCU). Su propósito es capturar datos de múltiples sensores analógicos y digitales, procesarlos localmente con filtrado y calibración, y transmitirlos de forma estructurada mediante el protocolo MQTT sobre redes Wi-Fi. En escenarios donde la conectividad inalámbrica no está disponible, puede operar en modo bridge serial para enviar los datos a través de un gateway BLE conectado a su puerto serie secundario.

---

## 2. Audiencia Objetivo

- Integradores de sistemas IoT industriales.
- Técnicos de telemetría y monitoreo remoto.
- Instaladores de equipos en campo (configuración inicial vía portal web).

---

## 3. Plataforma de Hardware

### 3.1 Microcontrolador

| Componente | Especificación |
|:---|:---|
| **MCU** | ESP32 (Xtensa LX6 de 32 bits, 2 núcleos) |
| **Placa** | NodeMCU (ESP32-DevKitC compatible) |
| **Frecuencia** | 240 MHz |
| **SRAM** | 512 KB |
| **Flash** | 4 MB (con tabla de particiones personalizada) |

### 3.2 Pines y Periféricos

| GPIO | Función | Tipo |
|:---|:---|:---|
| GPIO 0 | Botón de Configuración (AP) | Entrada (Pull-Up) |
| GPIO 2 | LED de Estado | Salida |
| GPIO 4 | Bus 1-Wire (DS18B20) | E/S Digital |
| GPIO 25 | Alimentación Conmutable (Sensores + Expansora) | Salida |
| GPIO 32 | UART2 RX (Secundario / Modbus) | Entrada Serial |
| GPIO 33 | UART2 TX (Secundario / Modbus) | Salida Serial |
| GPIO 34 | ADC Batería (18650) | Entrada Analógica |
| GPIO 35 | ADC Canal 0 | Entrada Analógica |
| GPIO 36 | ADC Canal 1 | Entrada Analógica |
| GPIO 39 | ADC Canal 2 | Entrada Analógica |

---

## 4. Requisitos Funcionales

### RF-1: Conectividad Wi-Fi y MQTT

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-1.1 | El dispositivo debe conectarse a una red Wi-Fi local con credenciales almacenadas en NVS. | Alta |
| RF-1.2 | Debe publicar datos de telemetría en formato JSON a un broker MQTT configurable. | Alta |
| RF-1.3 | Debe mantener un tópico Keep-Alive con estado general del dispositivo. | Alta |
| RF-1.4 | El usuario debe poder deshabilitar Wi-Fi mediante comando (`DVL+EN_WIFI=0`) para evitar reintentos en entornos sin cobertura. | Media |

### RF-2: Portal Web de Configuración (Modo AP)

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-2.1 | Al iniciar sin credenciales Wi-Fi o al presionar el botón GPIO 0 por 5 segundos, debe entrar en modo Access Point. | Alta |
| RF-2.2 | Debe proveer un portal web (puerto 80) para escanear redes e ingresar credenciales. | Alta |
| RF-2.3 | Debe proveer un portal de administración (puerto 8080) para configurar el broker MQTT y ver lecturas en vivo. | Media |

### RF-3: Lectura de Sensor de Temperatura (DS18B20)

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-3.1 | Leer temperatura desde sensor DS18B20 mediante bus 1-Wire (GPIO 4). | Alta |
| RF-3.2 | Implementar ciclo de energía controlado (GPIO 25) para reiniciar el bus si se detectan fallos de comunicación. | Alta |
| RF-3.3 | Validar lecturas fuera de rango (-20 a +50 °C) y descartar valores inválidos. | Alta |

### RF-4: Puerto Serial Secundario (UART2)

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-4.1 | El puerto UART2 (GPIO 32/33) debe ser configurable en velocidad (1200-115200 baud). | Alta |
| RF-4.2 | **Modo 1 (Lectura Expansora):** Leer tramas ASCII delimitadas por marcadores de inicio/fin configurables y separador configurable, soportando hasta 16 canales (S0..S15). | Alta |
| RF-4.3 | **Modo 2 (Bridge Serial):** Cuando Wi-Fi está caído o deshabilitado, transmitir todos los JSONs de telemetría por UART2 hacia un gateway BLE conectado. | Media |
| RF-4.4 | **Modo Modbus RTU Master:** Interrogar cíclicamente hasta 5 tramas Modbus configuradas (esclavo, función, dirección, cantidad). Excluyente con modo serial. | Alta |

### RF-5: Canales Analógicos (ADC)

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-5.1 | Monitorear tensión de batería interna (GPIO 34). | Alta |
| RF-5.2 | Leer 3 canales analógicos independientes (GPIO 35, 36, 39) con calibración lineal configurable (pendiente + offset). | Alta |
| RF-5.3 | Aplicar filtro de promedio móvil configurable para reducir ruido eléctrico. | Media |

### RF-6: Escaneo de Sensores BLE

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-6.1 | Escanear balizas BLE compatibles (Moko) en segundo plano y reportar datos de acelerómetro, temperatura, humedad, iBeacon, etc. | Alta |
| RF-6.2 | Publicar cada detección inmediatamente por MQTT o por bridge serial si corresponde. | Alta |

### RF-7: Comandos Remotos y Locales

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-7.1 | Procesar comandos de configuración recibidos por MQTT (tópico `COMANDOS`) y por puerto serial primario (USB). | Alta |
| RF-7.2 | Publicar respuestas a comandos en el tópico `RESPUESTA`. | Alta |
| RF-7.3 | Soportar comandos de habilitación/deshabilitación de módulos, configuración de parser serial, calibración ADC, parámetros Modbus, y actualización FOTA. | Alta |

### RF-8: Actualización de Firmware OTA

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RF-8.1 | Actualizar firmware de forma remota mediante URL enviada al tópico `FOTA`. | Alta |
| RF-8.2 | La URL de FOTA debe persistirse en NVS para futuras referencias. | Media |

---

## 5. Requisitos No Funcionales

| ID | Descripción | Prioridad |
|:---|:---|:---|
| RNF-1 | **Watchdog:** Temporizador por hardware configurado a 120 segundos para reinicio automático ante bloqueos. | Alta |
| RNF-2 | **Persistencia:** Toda configuración debe almacenarse en memoria NVS (Preferences) y sobrevivir a reinicios. | Alta |
| RNF-3 | **Buferización:** Si MQTT no está disponible, los mensajes deben almacenarse en SPIFFS (buffer circular de 1440 paquetes) y reenviarse cuando la conexión se restablezca. | Alta |
| RNF-4 | **Exclusión mutua:** Los módulos Modbus y Serial son excluyentes entre sí. El BLE al habilitarse desactiva Sensor y Modbus, pero preserva el modo bridge serial. | Alta |
| RNF-5 | **Versionado:** El firmware debe incluir un número de versión visible en los JSONs de telemetría. | Media |

---

## 6. Arquitectura del Firmware

### 6.1 Diagrama de Bloques

```mermaid
graph TD
    subgraph "Entradas"
        DS18B20["DS18B20 (GPIO 4)"]
        ADC_CH["ADC 3 Canales (GPIO 35,36,39)"]
        BATERIA["Batería (GPIO 34)"]
        BLE_SCAN["Escáner BLE"]
        UART2_IN["UART2 RX (GPIO 32)<br/>Expansora / Modbus"]
    end

    subgraph "Procesamiento"
        FILTRO["Filtro y Calibración ADC"]
        PARSER["Parser Serial<br/>(Start/End Markers)"]
        MODBUS["Modbus RTU Master"]
        CMD["Procesador de Comandos"]
    end

    subgraph "Salidas"
        MQTT_OUT["MQTT<br/>(Wi-Fi)"]
        BRIDGE_OUT["Bridge Serial<br/>UART2 TX (GPIO 33)"]
        FLASH_BUF["Buffer SPIFFS"]
        LED["LED Estado (GPIO 2)"]
    end

    DS18B20 --> CMD
    ADC_CH --> FILTRO --> CMD
    BATERIA --> FILTRO --> CMD
    BLE_SCAN --> CMD
    UART2_IN --> PARSER --> CMD
    UART2_IN --> MODBUS --> CMD

    CMD --> MQTT_OUT
    CMD --> BRIDGE_OUT
    CMD --> FLASH_BUF
    CMD --> LED
```

### 6.2 Flujo de Datos

1. Los módulos de entrada (DS18B20, ADC, BLE, UART2) capturan datos crudos.
2. El procesador de comandos aplica filtros, calibración y formato.
3. En cada ciclo de publicación (intervalo configurable), los JSONs se construyen y se envían:
   - Por **MQTT** si hay conexión Wi-Fi.
   - Por **bridge serial (UART2)** si el modo bridge está activo y Wi-Fi está caído.
   - Al **buffer SPIFFS** si no hay ninguna salida disponible.

---

## 7. Protocolo MQTT

### 7.1 Tópicos

| Dirección | Tópico | Propósito |
|:---|:---|:---|
| Publicación | `DVL/NODEMCU/<ident>` | Keep-Alive / Estado general |
| Publicación | `DVL/NODEMCU/<ident>/DS18B20` | Reporte de temperatura |
| Publicación | `DVL/NODEMCU/<ident>/SERIAL` | Datos del puerto serial secundario (S0..S15) |
| Publicación | `DVL/NODEMCU/<ident>/MODBUS` | Respuestas Modbus |
| Publicación | `DVL/NODEMCU/<ident>/ADC` | Reporte de canales ADC |
| Publicación | `DVL/NODEMCU/<ident>/BLE/<MAC>/...` | Datos de sensores BLE detectados |
| Publicación | `DVL/NODEMCU/<ident>/RESPUESTA` | Respuesta a comandos |
| Suscripción | `DVL/NODEMCU/<ident>/COMANDOS` | Recepción de comandos remotos |
| Suscripción | `DVL/NODEMCU/<ident>/FOTA` | URL de actualización de firmware |

### 7.2 Formato JSON (Ejemplo Keep-Alive)

```json
{
  "topic": "DVL/NODEMCU/OBU_123",
  "ident": "OBU_123",
  "status": "keep-alive",
  "date": "2026-06-11 12:00:00",
  "Version": "V05.03.01",
  "reboot_count": 42,
  "conn_type": "WIFI",
  "RSSI": "-65"
}
```

---

## 8. Comandos de Configuración

| Comando | Descripción |
|:---|:---|
| `DVL+EN_SERIAL=<0\|1\|2>` | 0=deshabilitado, 1=lectura expansora, 2=bridge serial |
| `DVL+EN_WIFI=<1\|0>` | Habilitar/deshabilitar Wi-Fi |
| `DVL+EN_SENSOR=<1\|0>` | Habilitar/deshabilitar sensor DS18B20 |
| `DVL+EN_MODBUS=<1\|0>` | Habilitar/deshabilitar Modbus (excluye serial) |
| `DVL+EN_BLE=<1\|0>` | Habilitar/deshabilitar escáner BLE |
| `DVL+EN_ADC=<1\|0>` | Habilitar reporte ADC independiente |
| `DVL+SBAUD=<baud>` | Configurar baud rate del UART2 |
| `DVL+SFINI=<txt>` | Marcador de inicio de trama serial |
| `DVL+SFFIN=<txt>` | Marcador de fin de trama serial |
| `DVL+SPARSE=<char>` | Separador de campos serial |
| `DVL+STIME=<seg>` | Intervalo de publicación MQTT |
| `DVL+RESET` | Reiniciar el dispositivo |
| `DVL+EXP_RESET` | Reiniciar la placa expansora |
| `DVL+FOTA=<url>` | Iniciar actualización OTA |
| `EXP+<cmd>` | Enviar comando directo a la expansora |
| `DVL+QEN_SERIAL`, `DVL+QEN_WIFI`, etc. | Consultar estado de módulos |

Ver `COMANDOS.md` para la lista completa.

---

## 9. Modo Bridge Serial (EN_SERIAL=2)

Cuando se activa el modo bridge (`DVL+EN_SERIAL=2`), el firmware cambia su comportamiento:

- **Deja de leer** la expansora (solo aplica en modo 1).
- Cuando **Wi-Fi está caído o deshabilitado**, todos los JSONs de telemetría se envían por UART2 (GPIO 33) a 4800 baud (configurable).
- Cuando Wi-Fi está disponible, los JSONs se envían por MQTT normalmente.
- El bridge es compatible con el escáner BLE: los datos BLE también se redirigen por UART2.

**Caso de uso típico:** El NodeMCU está en un área sin Wi-Fi. Un gateway BLE conectado al UART2 recibe los JSONs y los retransmite por BLE a un concentrador.

```
DVL+EN_SERIAL=2    → Activar bridge serial
DVL+EN_WIFI=0      → Deshabilitar Wi-Fi (opcional)
DVL+EN_BLE=1       → Habilitar escáner BLE
```

---

## 10. Casos de Uso Principales

| CU | Descripción |
|:---|:---|
| CU-1 | **Monitoreo de temperatura ambiental:** Sensor DS18B20 reporta cada 60 segundos por MQTT. |
| CU-2 | **Telemetría multi-canal vía expansora:** Placa expansora envía 16 canales analógicos/digitales por UART2. |
| CU-3 | **Modbus RTU industrial:** Consulta cíclica de registros en esclavos Modbus conectados al bus RS-485. |
| CU-4 | **Bridge serial para zonas sin cobertura:** Equipo en campo sin Wi-Fi envía datos por UART2 a gateway BLE. |
| CU-5 | **Configuración en campo:** Técnico conecta vía portal web (AP) para configurar Wi-Fi y broker MQTT. |
| CU-6 | **Escaneo de balizas BLE:** Detección de sensores Moko en interiores con reporte inmediato. |

---

## 11. Consideraciones Futuras

- Cifrado TLS para conexiones MQTT.
- Almacenamiento de datos históricos en tarjeta SD.
