# Manual de Producción: Compilación y Carga de Firmware en Dispositivos LilyGo

Este documento detalla el procedimiento para compilar, exportar el binario (`.bin`) y realizar la carga directa de firmware en los dispositivos LilyGo utilizando **Arduino IDE**. Este proceso salta la necesidad de realizar actualizaciones remotas (FOTA) y es el recomendado para la configuración inicial en la línea de producción.

---

## 1. Pre-Requisitos de Entorno

Antes de iniciar, el puesto de producción debe contar con:
1. **Arduino IDE** (Versión 2.0 o superior recomendada).
2. **Drivers USB-Serial** instalados en la PC de producción (generalmente chips **CH9102**, **CH340** o **CP2102**, comunes en placas LilyGo).
3. **Cable de datos USB-C** de buena calidad.

---

## 2. Configuración en Arduino IDE

1. Conecte el dispositivo LilyGo mediante el cable USB-C a la PC.
2. Abra el proyecto abriendo el archivo principal `main.ino` en el Arduino IDE.
3. En el menú superior, configure los parámetros de la placa y puerto COM:
   * **Placa (Board):** Seleccione el modelo específico (ej. `ESP32 Dev Module` o la placa LilyGo correspondiente).
   * **Puerto (Port):** Seleccione el puerto COM asignado al dispositivo (ej. `COM3`, `COM4`, etc.). Puede identificarlo desconectando y volviendo a conectar el cable para ver cuál puerto aparece de nuevo.

---

## 3. Proceso de Compilación y Carga Directa (Upload)

Para cargar el programa directamente desde el entorno de desarrollo al dispositivo conectado:

1. Asegúrese de que la versión del firmware en el código (`versionado = "V03.05.XX"`) sea la correcta para esta tanda de producción.
2. Haga clic en el botón **Subir (Upload)** (el ícono de flecha hacia la derecha `→` en la barra de herramientas superior) o use el atajo de teclado:
   * **Windows/Linux:** `Ctrl + U`
3. El IDE comenzará a compilar el proyecto. Esto puede tardar unos minutos en la primera ejecución.
4. Una vez compilado, iniciará la fase de escritura. Verá un progreso en la sección de salida (*Output*) con líneas de este estilo:
   ```text
   Writing at 0x00010000... (10 %)
   Writing at 0x00020000... (20 %)
   ...
   Hash of data verified.
   Leaving...
   Hard resetting via RTS pin...
   ```
5. Al finalizar, aparecerá el mensaje **"Subida finalizada" (Upload finished)**. El equipo se reiniciará automáticamente y comenzará a ejecutar el nuevo código.

---

## 4. Exportar el Binario Compilado (.bin) para Duplicación rápida

Si desea generar el archivo binario final para poder clonarlo en otros dispositivos sin necesidad de compilar el código cada vez:

1. En Arduino IDE, vaya al menú superior:
   * **Programa** -> **Exportar binario compilado** (o presione `Ctrl + Alt + S`).
2. El IDE compilará el código y generará los archivos binarios dentro del directorio del proyecto (en la carpeta `build` o directamente junto al archivo `main.ino`).
3. Los archivos generados clave son:
   * `main.ino.bin`: El binario del firmware listo para cargar a la dirección `0x10000`.
   * `main.ino.bootloader.bin`: El cargador de arranque necesario para inicializaciones limpias de hardware (dirección `0x1000`).
   * `main.ino.partitions.bin`: La tabla de particiones del dispositivo (dirección `0x8000`).

*(Estos archivos exportados pueden ser entregados al sector de producción para su grabación masiva a través de herramientas rápidas de consola o herramientas gráficas sin abrir el código fuente).*

---

## 5. Resolución de Problemas Comunes en Producción

* **Error: "Failed to connect to ESP32: Timed out waiting for packet header":**
  * **Causa:** La placa no entró en modo de descarga (bootloader) automáticamente.
  * **Solución:** Mantenga presionado el botón **BOOT / RST** de la placa LilyGo, presione el botón de Subir en el IDE, y suelte el botón de la placa en cuanto vea en la terminal el mensaje de conexión (`Connecting...`).
* **El puerto COM no aparece:**
  * **Causa:** Falta de drivers o cable de solo carga.
  * **Solución:** Reemplace el cable USB-C por uno que soporte transferencia de datos. Instale los controladores del fabricante CH340 / CP2102 correspondientes si la PC no reconoce el dispositivo en el Administrador de Dispositivos.
