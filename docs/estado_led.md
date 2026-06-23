# Comportamiento del LED Indicador de Estado (Blinkeo)

Este documento detalla el comportamiento del LED incorporado (`LED_PIN = 2`) en el firmware de la **LILY-GO (Gateway)** y del **NodeMCU (Satélite)**. El blinkeo (parpadeo) del LED refleja de forma visual e inmediata el estado de las conexiones de red (WiFi/GPRS) y la conexión con el Broker MQTT.

---

## 1. Tabla de Resumen de Estados

El comportamiento del LED se divide en dos categorías principales: cuando el reporte MQTT está activo y conectado, y cuando el equipo está desconectado de MQTT pero enlazado a alguna de las interfaces de red.

| Estado de Conexión | Red Activa | Comportamiento del LED | Frecuencia / Ciclo | Descripción Visual |
| :--- | :--- | :--- | :--- | :--- |
| **MQTT Conectado** | **WiFi** | **Principalmente Encendido** con 1 micro-apagado. | Ciclo de 5 segundos | Encendido continuo con un destello corto de apagado (100 ms) cada 5 segundos. |
| **MQTT Conectado** | **GPRS (Módem)** | **Principalmente Encendido** con 2 micro-apagados. | Ciclo de 5 segundos | Encendido continuo con dos destellos cortos de apagado (100 ms c/u, separados por 100 ms) cada 5 segundos. |
| **MQTT Desconectado** | **WiFi** | **Triple parpadeo veloz** de encendido. | Ciclo de 2 segundos | Tres destellos cortos de encendido (100 ms c/u) al inicio de un ciclo de 2 segundos. |
| **MQTT Desconectado** | **GPRS (Módem)** | **Parpadeo simple y corto** de encendido. | Ciclo de 2 segundos | Un destello corto de encendido (200 ms) al inicio de un ciclo de 2 segundos. |
| **Sin Conexión** | **Ninguna** | **Completamente Apagado** | Permanente | El LED permanece apagado. |

> [!NOTE]
> Dado que la placa **NodeMCU (Satélite)** no cuenta con módem celular GPRS, las opciones correspondientes a GPRS no aplican a dicho hardware; en su lugar, se rige únicamente por los estados de WiFi y Desconectado.

---

## 2. Diagramas de Pulso (Secuencias de Tiempo)

A continuación se muestra el pulso eléctrico del LED en cada estado (donde `█` representa LED encendido y `_` representa LED apagado).

### A. MQTT Conectado (Operación Normal Activa)
En este modo, el LED sirve como indicador de presencia. El hecho de estar mayormente encendido confirma que el sistema corre correctamente, y el micro-apagado confirma la transmisión saludable al Broker MQTT.

* **Conexión por WiFi (1 apagado cada 5 segundos):**

```text
0s      1s      2s      3s      4s      5s
[ _██████████████████████████████████████ ]
  ↑
  Apagado 100ms
```

* **Conexión por GPRS (2 apagados cada 5 segundos):**

```text
0s      1s      2s      3s      4s      5s
[ _█_████████████████████████████████████ ]
  ↑ ↑
  Dos pulsos de apagado de 100ms separados por 100ms de encendido
```

---

### B. MQTT Desconectado pero Red Conectada (Fase de Sincronización)
En este modo, los destellos indican que la interfaz física de red está activa, pero el canal de telemetría MQTT aún no se ha podido enlazar o se ha perdido la conexión.

* **Conectado a WiFi (Triple parpadeo rápido cada 2 segundos):**

```text
0ms   200ms 400ms 600ms  1.0s         2.0s
[ ███_███_███_________________________ ]
  ↑   ↑   ↑
  Tres destellos cortos de 100ms
```

* **Conectado a GPRS (Un destello cada 2 segundos):**

```text
0ms   200ms              1.0s         2.0s
[ ██████______________________________ ]
  ↑
  Un destello corto de 200ms
```

---

### C. Sin Conexión de Red o FOTA en curso (Inactivo / Silencioso)
* **Estado:** LED completamente apagado.

```text
[ ____________________________________ ]
```

---
