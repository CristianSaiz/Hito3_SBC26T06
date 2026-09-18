# Hito 3: Conectividad Wi-Fi (STA/AP), Servidor Web de Provisión, BLE (NimBLE) y Telemetría Multihilo

**Asignatura:** Sistemas Basados en Computador (SBC) — UPM  
**Plataforma de Hardware:** ESP32-WROOM-32 (DevKitC v4)  
**Entorno de Desarrollo:** ESP-IDF v5.3.1 (C Native Build System)  
**Grupo:** SBC26T06  

---

## 📌 Descripción del Proyecto

Este firmware implementa un sistema embebido IoT tolerante a fallos (*Failover*) con arquitectura multitarea sobre **FreeRTOS**. El sistema captura telemetría analógica del sensor de luminosidad (LDR) compensada mediante calibración **eFuse (Line-Fitting)** y la presenta en tiempo real sobre una pantalla **OLED I2C (SSD1306)** y un **display de 7 segmentos (Ánodo Común)**.

Si el dispositivo pierde la conectividad Wi-Fi o las credenciales almacenadas en la memoria Flash son erróneas, el firmware conmuta automáticamente a un modo de **Provisión Dual** activando un **Portal Cautivo HTTP** sobre un Punto de Acceso local (`ESP32_XXXX`) y un canal secundario vía **Bluetooth Low Energy (BLE)** utilizando la pila **NimBLE**.

---

## 🏗️ Arquitectura de Software Embebido &amp; Asignación de Núcleos (FreeRTOS)

Para garantizar un comportamiento determinista y prevenir interferencias de la pila de radio sobre la instrumentación física, las tareas se encuentran asignadas estrictamente a los núcleos del microcontrolador:

```

```
                      ┌──────────────────────────────────────────────┐
                      │               ESP32 DUAL-CORE                │
                      └──────────────────────┬───────────────────────┘
                                             │
              ┌──────────────────────────────┴──────────────────────────────┐
              ▼                                                             ▼
 Core 0 (PRO_CPU) - Comms &amp; FSM                             Core 1 (APP_CPU) - Instrumentación

```

┌─────────────────────────────────────────┐ ┌─────────────────────────────────────────┐ │ • Pila Wi-Fi (WIFI\_MODE\_STA / SoftAP) │ │ • adc\_task (ADC1\_CH6 Oneshot + eFuse) │ │ • Servidor HTTP embebido (Puerto 80) │ │ • display\_task (OLED I2C + 7-Seg) │ │ • Stack BLE NimBLE (GATT Server) │ └────────────────────▲────────────────────┘ │ • System Event Loop &amp; FreeRTOS EventGrp │ │ └─────────────────────────────────────────┘ Cola IPC (xQueue) (ldr\_telemetry\_t, overwrite)

```

1. **Core 0 (`PRO_CPU`):** Dedicado en exclusiva a las pilas de comunicación inalámbrica (Wi-Fi STA/AP, TCP/IP LWIP, Servidor HTTP, NimBLE BLE) y la gestión del Event Loop [cite: 3].
2. **Core 1 (`APP_CPU`):** Ejecuta la tarea productora `adc_task` (muestreo Oneshot a 2 Hz) y la tarea consumidora `display_task` (refresco visual de displays) [cite: 3, 7].
3. **Comunicación Inter-Tarea (IPC):** Se emplea una cola de FreeRTOS (`QueueHandle_t`) para el paso de estructuras de telemetría de forma segura y desvinculada del tiempo de ejecución [cite: 7].

---

## 🔄 Máquina de Estados Finita (FSM) de Conectividad

```

```
           ┌────────────────────────┐
           │     Inicio / Boot      │
           └───────────┬────────────┘
                       │
                       ▼
         ┌───────────────────────────┐
         │ Lectura SPIFFS            │
         │ (/spiffs/wifi_config.txt) │
         └─────────────┬─────────────┘
                       │
                       ▼
         ┌───────────────────────────┐  ¿Exitoso?   ┌───────────────────────────────┐
         │ Intentar Conexión STA     ├─────────────►│ ESTADO OPERATIVO NORMAL       │
         │ (Máx. 5 Reintentos)       │   (GOT_IP)   │ (Red Local OK + Telemetría)   │
         └─────────────┬─────────────┘              └───────────────────────────────┘
                       │ Fallo (Reintentos exhaustos)
                       ▼
         ┌──────────────────────────────────────────┐
         │ ESTADO FAILOVER PROVISIÓN                │
         │ • SoftAP Activo (SSID: ESP32_XXXX)       │
         │ • Servidor Web HTTP (Portal Cautivo)    │
         │ • Anuncios BLE (SSID: ESP32_BLE_PROV)    │
         └──────────────────────────────────────────┘

```

```

---

## 💾 Memoria NOR Flash &amp; Sistema de Archivos SPIFFS

El mapa de memoria se gestiona mediante una tabla de particiones personalizada (`partitions.csv`) de 4 MB [cite: 3]:

* **`nvs` (24 KB):** Almacenamiento de parámetros RF y claves de calibración Wi-Fi.
* **`factory` (1.93 MB):** Ejecutable principal del firmware C.
* **`spiffs` (1 MB):** Sistema de archivos virtual (VFS) donde residen los recursos web y de configuración:
  * `/spiffs/wifi_config.txt`: Credenciales guardadas en formato plano `SSID=xxx` / `PASS=yyy`.
  * `/spiffs/index.html`: Interfaz web responsiva del Portal Cautivo.
  * `/spiffs/style.css`: Hoja de estilos CSS.

---

## 🛠️ Estructura del Proyecto

```text
.
├── CMakeLists.txt                  # Configuración CMake raíz del proyecto
├── sdkconfig.defaults              # Parámetros por defecto (Custom Partitions + 4MB Flash + NimBLE)
├── partitions.csv                  # Tabla de particiones personalizada (1MB SPIFFS)
├── README.md                       # Documentación técnica
├── spiffs/                         # Directorio empaquetado automáticamente en la Flash
│   ├── index.html                  # Formulario web del Portal Cautivo
│   ├── style.css                   # Estilos responsivos
│   └── wifi_config.txt             # Credenciales por defecto
└── main/
    ├── CMakeLists.txt              # Registro de componentes y spiffs_create_partition_image
    ├── main.c                      # Punto de entrada, FSM y tareas FreeRTOS
    ├── include/
    │   ├── adc_sampler.h           # Driver ADC1 Oneshot + eFuse
    │   ├── ble_prov.h              # Gestor BLE NimBLE GATT
    │   ├── display_7seg.h          # Driver 7 Segmentos (Ánodo Común)
    │   ├── oled_display.h          # Driver I2C OLED SSD1306
    │   ├── spiffs_manager.h        # Driver VFS SPIFFS con protección Mutex
    │   ├── web_server.h            # Servidor HTTP embebido
    │   └── wifi_manager.h          # Gestor Wi-Fi STA / SoftAP
    └── src/
        ├── adc_sampler.c
        ├── ble_prov.c
        ├── display_7seg.c
        ├── oled_display.c
        ├── spiffs_manager.c
        ├── web_server.c
        └── wifi_manager.c

```

---

## 🚀 Instrucciones de Compilación y Grabación

1. **Configurar entorno de ESP-IDF (v5.3.1):**

```
get_idf

```

1. **Compilar el proyecto y empaquetar la imagen SPIFFS:**

```
idf.py build

```

1. **Flashear binario y partición de datos en el ESP32:**

```
idf.py -p /dev/ttyUSB0 flash

```

1. **Abrir el monitor Serie para depuración (115200 baudios):**

```
idf.py -p /dev/ttyUSB0 monitor

```

```

---

### 🧪 PASO 2: Guía de Pruebas y Validación en el Laboratorio

Para comprobar el funcionamiento correcto del sistema antes de grabar el vídeo o defender el proyecto frente al tribunal:

#### **Prueba A: Conexión Normal en Modo STA**
1. Escribe en `spiffs/wifi_config.txt` las credenciales de tu red Wi-Fi o del móvil en zona de cobertura (`SSID` y `PASS`).
2. Compila y flashea (`idf.py flash monitor`).
3. **Resultado esperado:** En la consola verás los trazos de log `IP_EVENT_STA_GOT_IP` indicando la IP asignada, la pantalla OLED mostrará la IP y la luminosidad en Luxes, y el display de 7 segmentos irá cambiando según la luz que reciba el LDR [cite: 3].

#### **Prueba B: Conmutación a Modo Failover (SoftAP + HTTP + BLE)**
1. Apaga el punto de acceso Wi-Fi o cambia la contraseña en `spiffs/wifi_config.txt` por una errónea.
2. Reinicia el ESP32.
3. **Resultado esperado:**
   * El sistema intentará conectar 5 veces y mostrará el aviso `Reintentos agotados` [cite: 3].
   * Se detendrá la red STA y arrancará el **SoftAP** con la red abierta `ESP32_XXXX` [cite: 3].
   * Conéctate con el móvil o PC a la red Wi-Fi `ESP32_XXXX`, abre el navegador e introduce `http://192.168.4.1`. Aparecerá el **Portal Cautivo** [cite: 3].
   * Abre una app de Bluetooth (como *nRF Connect*) y escanea: verás el dispositivo anunciándose como **`ESP32_BLE_PROV`** [cite: 3].
   * Introduce nuevas credenciales válidas en el Portal Cautivo y pulsa **Guardar y Conectar**. El ESP32 guardará el archivo en SPIFFS, se reiniciará automáticamente y conectará a la red nueva [cite: 3].

---

### 📦 PASO 3: Auditoría de Control de Entregas UPM

De acuerdo con las **Instrucciones de Entrega Oficiales de la UPM** [cite: 115]:

1. **Excluir la carpeta `build/`:**  
   Antes de comprimir o subir el repositorio a SharePoint, debes borrar la carpeta de compilación para cumplir con las normas de peso y limpieza [cite: 115]:
   ```bash
   idf.py fullclean

```

1. **Vídeo Demostrativo (3 Minutos max.):** Prepara un clip corto donde se aprecie [cite: 115]:
  * Arranque del sistema.
  * Transición a modo Failover cuando falla la red Wi-Fi.
  * Provisión desde el móvil entrando al servidor web `192.168.4.1`.
  * Cambio de luxes en la pantalla OLED y el display de 7 segmentos al tapar/iluminar la LDR [cite: 3, 7].
2. **Enlace a SharePoint / Moodle:** Verifica que los permisos del enlace a la carpeta de SharePoint estén en modo *"Cualquier usuario con el enlace puede ver"* para que el tribunal pueda evaluarlo sin bloqueos [cite: 115].