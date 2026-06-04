# Product Requirements Document (PRD) - LILY-GO Firmware

## 1. Introducción
El proyecto LILY-GO consiste en un firmware desarrollado para microcontroladores ESP32 (específicamente orientado a las placas Lily-Go con soporte para módem celular integrado). Su propósito principal es actuar como una unidad de telemetría, monitoreo y control, con conectividad robusta a través de WiFi y redes celulares (GSM/GPRS).

## 2. Objetivos del Producto
- **Monitoreo Confiable:** Proveer un sistema de adquisición de datos (como estados de pines, voltaje de ADC) y reportarlos periódicamente.
- **Conectividad Dual:** Garantizar la transmisión de datos priorizando redes WiFi, con un respaldo de conexión GPRS a través de un módem (SIM7000 o compatible).
- **Control de Alarmas (Sistema PIVOT):** Implementar lógica de monitoreo de estado para sistemas de maquinarias externas (Pivots de riego), accionamiento de sirenas con enclavamiento, tiempos de retardo y duraciones configurables.
- **Eficiencia Energética:** Soportar modos de bajo consumo (Deep Sleep) prolongados para escenarios de alimentación limitada, garantizando que el equipo despierte ante eventos críticos.
- **Gestión Remota:** Permitir configuraciones a través de comandos por consola serie y ofrecer soporte para actualizaciones Firmware Over-The-Air (FOTA).

## 3. Arquitectura y Componentes
- **Microcontrolador:** ESP32.
- **Módem Celular:** Módulo de la familia SIM (controlado a través de la librería TinyGsmClient).
- **Almacenamiento Local:** Uso de la memoria Flash interna para el almacenamiento (buffer circular) de tramas MQTT generadas durante períodos de desconexión.
- **Protocolo de Comunicación:** MQTT para el envío y recepción bidireccional de telemetría y comandos de forma ligera.
- **Configuración Persistente:** Uso de la partición NVS (Preferences.h) para retener configuraciones de usuario (habilitación de módulos, timers, latitud/longitud manuales) ante reinicios de energía.

## 4. Funcionalidades Principales

### 4.1. Conectividad y Redundancia
- **Lógica de Conexión:** Intenta conectarse a WiFi como capa primaria. Si no hay conexión o falla repetidamente, enciende el módem GPRS y establece un contexto PDP para proveer internet.
- **Reconexión MQTT:** Bucle constante de verificación de conexión con el broker. En caso de múltiples fallos consecutivos del módem intentando conectar al broker MQTT, el sistema reinicia el módem físicamente.
- **Buffering Offline:** Todos los reportes generados (ADC, Alarmas PIVOT) se graban en Flash en formato JSON. Al recuperar la red MQTT, se descargan secuencialmente y se publican en el broker.

### 4.2. Sistema PIVOT y Sirena
- **Monitoreo de Entradas:** Lectura de dos entradas digitales (`IN_1` pin 13, `IN_2` pin 14) con lógica de filtro anti-rebote mediante software (debounce continuo de 3 segundos).
- **Alarma Lógica:** Se evalúa condición de alarma en caso de que las variables de entrada alcancen un estado de falla predeterminado.
- **Control de Sirena (PIN 15):** 
  - **Retraso (Delay - `DSIR`):** Tiempo de espera paramétrico antes de activar la salida física de la sirena tras detectar un evento.
  - **Enclavamiento:** La sirena permanece encendida por lógica de enclavamiento independientemente de si los pines de entrada retornan a su nivel normal.
  - **Timeout (`TSIR`):** Tiempo límite de activación. Al concluir el tiempo, la sirena se apaga para prevenir daños acústicos/eléctricos y el sistema de alarma entra en modo de "Deshabilitación Automática" para forzar una intervención manual o remota (Comando PIVON).

### 4.3. Modo de Bajo Consumo (Deep Sleep)
- **Activación Condicionada:** Si el Deep Sleep está parametrizado, el sistema PIVOT está habilitado, pero NO hay ninguna alarma activa, el equipo entra en letargo tras enviar un reporte MQTT exitoso.
- **Gestión de Energía:** El módem GPRS se apaga explícitamente mediante comando AT antes del ciclo de Deep Sleep.
- **Wake Up:** Despertador basado en un Timer (configurable) o vía Wake-Up Ext1 ante la caída de cualquiera de los pines lógicos del Pivot a un nivel `LOW`, lo cual indica una alerta en tiempo real.

### 4.4. NTP y Sincronización Horaria
- Sincronización obligatoria del Reloj en Tiempo Real interno utilizando servidores NTP, tanto bajo interfaz WiFi como usando la red GPRS.
- Bloqueo de emisión de reportes iniciales al salir de Deep Sleep hasta no lograr sincronizar la hora correcta.

### 4.5. Reportes de Telemetría (MQTT JSON)
Se contemplan múltiples sub-tópicos de MQTT para mantener organizados los JSON:
- **Keep Alive (`/`):** Reporte general del estado del dispositivo (RSSI, Tecnología WiFi/GSM, IMEI, IMSI, ICCID, versión de firmware, tiempo sin reportes).
- **ADC (`/ADC`):** Reporte de los canales de voltaje internos.
- **PIVOT (`/PIVOT`):** Estado de las entradas lógicas, la salida de la sirena y si el sistema está armado (1) o desarmado (0).

### 4.6. CLI Integrada y Consola Transparente
- Interfaz sobre el puerto serie nativo estructurada con comandos (estilo AT personalizados: `DVL+`) para manipular tiempos, leer GPS de respaldo, habilitar módulos, o alterar los retardos.
- **Modo Passthrough (`DVL+PASSON`):** Establece un puente de puerto serial transparente hacia el módem celular para permitir tareas de depuración en bajo nivel directamente sobre el SIM7000.

## 5. Requisitos No Funcionales
- **Robustez (WDT):** Watchdog Timer por hardware seteado en 120 segundos. El bucle central "alimenta" el WDT regularmente, por lo que cualquier bloqueo en rutinas de lectura de serial o bloqueos del TCP/IP fuerzan un soft-reset.
- **Gestor de Particiones:** Uso de tabla de particiones personalizada (`partitions.csv`) diseñada para disponer de memoria suficiente de `app` y `OTA` junto a un bloque saludable dedicado al SPIFFS.
