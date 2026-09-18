# Hito 3: Conectividad Wi-Fi (STA/AP), Servidor Web de Provisión, BLE (NimBLE) y Telemetría Multihilo

## Metadatos del Proyecto
* **Asignatura:** Sistemas Basados en Computador (SBC) - ETSISI UPM
* **Identificador de Grupo:** `SBC26T06` (**Grupo B**)
* **Miembros del Grupo B:**
  * Ana Cuevas
  * Cristian Saiz
* **Plataforma Hardware:** ESP32-WROOM-32 (Xtensa Dual-Core 32-bit LX6)
* **Framework y Entorno:** ESP-IDF v5.x / FreeRTOS Kernel
* **Lenguaje:** C (C99 / C11)
* **Repositorio GitHub:** [Enlace al repositorio](https://github.com/CristianSaiz/Hito3_SBC26T06)
* **Espacio de Trabajo SharePoint:** [Directorio del Grupo en SharePoint](https://upm365.sharepoint.com/:f:/s/SBC26T06/IgCtrEbZAATWSI3JIddTWiWQAcymd20dC3nxcNgg02HvnA4?e=Q3sIcr)
* **Vídeo Demostrativo:** [Enlace a la demostración (< 1 min)](https://upm365.sharepoint.com/:v:/s/SBC26T06/IQBVzSkdNLwHSaj4WkVzERdoAZRIPk3-6mojsQr8qt3T1Lw?e=Lu0EGH)

---

## 📌 Descripción del Proyecto

Este firmware implementa un sistema embebido IoT tolerante a fallos (*Failover Architecture*) con diseño multitarea determinista sobre **FreeRTOS**. El sistema adquiere telemetría ambiental del sensor analógico de luminosidad (LDR) compensado mediante calibración polinómica por **eFuse (Line-Fitting)**, procesando los datos para su visualización concurrente en tiempo real sobre:
1. Una pantalla **OLED I2C (SSD1306)** con representación numérica en Luxes, estado de red, IP asignada y barra porcentual gráfica.
2. Un display de **7 segmentos (Ánodo Común)** multiplexado por GPIO directo que refleja la intensidad lumínica escalada (0-9).

Si el nodo experimenta una pérdida de enlace Wi-Fi o las credenciales almacenadas en la memoria Flash no permiten la autenticación tras 5 reintentos, el firmware activa una contingencia de **Provisión Dual**:
* Levanta un Punto de Acceso local protegido (**SoftAP**) con IP estática en LwIP (`192.168.10.1`) y despliega un **Portal Cautivo HTTP** para la reconfiguración inalámbrica.
* Inicializa simultáneamente un canal secundario de aprovisionamiento vía **Bluetooth Low Energy (BLE)** utilizando la pila **NimBLE** (GATT Server).

---

## 🏗️ Arquitectura de Software Embebido & Asignación de Núcleos (FreeRTOS)

Para evitar que las interrupciones críticas de radiofrecuencia (RF) introduzcan fluctuaciones (*jitter*) en la adquisición y renderizado de periféricos, la carga computacional se segrega rígidamente entre los dos núcleos simétricos del microcontrolador:

```text
                      ┌──────────────────────────────────────────────┐
                      │               ESP32 DUAL-CORE                │
                      └──────────────────────┬───────────────────────┘
                                             │
             ┌───────────────────────────────┴───────────────────────────────┐
             ▼                                                               ▼
Core 0 (PRO_CPU) - Comms & FSM                                  Core 1 (APP_CPU) - Instrumentación
┌─────────────────────────────────────────┐                     ┌─────────────────────────────────────────┐
│ • Pila Wi-Fi (WIFI_MODE_STA / APSTA)    │                     │ • adc_task (ADC1_CH6 Oneshot + eFuse)   │
│ • Servidor HTTP embebido (Puerto 80)    │                     │   - Período: 500 ms (2 Hz)              │
│ • Stack BLE NimBLE (GATT Server)        │                     │   - Prioridad: 5 (FreeRTOS)             │
│ • System Event Loop (LwIP tcpip_thread) │                     ├─────────────────────────────────────────┤
│ • Máquina de Estados (app_main)         │                     │ • display_task (OLED I2C + 7-Seg)       │
└────────────────────▲────────────────────┘                     │   - Espera en cola (Event-Driven)       │
                     │                                          │   - Prioridad: 4 (FreeRTOS)             │
                     │                                          └────────────────────▲────────────────────┘
                     │                                                               │
                     └───────────────────────────┬───────────────────────────────────┘
                                                 │
                                     Cola IPC (xQueueOverwrite)
                                   tipo: ldr_telemetry_t (Len: 1)
```

1. Core 0 (PRO_CPU): Dedicado exclusivamente a la pila de comunicaciones (Wi-Fi Driver, LwIP TCP/IP, servidor HTTP, NimBLE BLE) y a la máquina de estados orquestadora en app_main.

2. Core 1 (APP_CPU): Ejecuta la tarea productora adc_task (muestreo a 2 Hz con compensación eFuse) y la tarea consumidora display_task (renderizado en OLED y multiplexación del display de 7 segmentos).

3. Comunicación Inter-Tarea (IPC): Se utiliza una cola FreeRTOS (QueueHandle_t) configurada con semántica de sobreescritura (xQueueOverwrite) y longitud unitaria, garantizando que el display siempre consuma la muestra más fresca sin retención de memoria ni bloqueos.

## 🔄 Máquina de Estados Finita (FSM) de Conectividad
La transición entre modos de red se orquesta de manera desacoplada mediante un grupo de eventos (EventGroupHandle_t):

```text
           ┌────────────────────────┐
           │      Inicio / Boot     │
           └───────────┬────────────┘
                       │
                       ▼
         ┌───────────────────────────┐
         │ Montaje VFS SPIFFS        │
         │ Lectura /wifi_config.txt  │
         └─────────────┬─────────────┘
                       │
                       ▼
         ┌───────────────────────────┐  ¿Conexión OK?   ┌───────────────────────────────┐
         │ Intentar Conexión STA     ├─────────────────►│ ESTADO OPERATIVO NORMAL       │
         │ (Máx. 5 Reintentos)       │   (GOT_IP)       │ • Wi-Fi STA enlazado          │
         └─────────────┬─────────────┘                  │ • Web Server Telemetría (P80) │
                       │                                └───────────────────────────────┘
                       │ Fallo de autenticación / Timeout
                       ▼
         ┌──────────────────────────────────────────┐
         │ ESTADO FAILOVER (PROVISIÓN LOCAL)        │
         │ • Detención de interfaz STA              │
         │ • SoftAP Seguro Activo (SSID: ESP32_XXXX)│
         │ • Subred LwIP Fija: 192.168.10.1         │
         │ • Servidor Web HTTP (Portal Cautivo)     │
         │ • Anuncios BLE NimBLE (ESP32_BLE_PROV)   │
         └──────────────────────────────────────────┘
```

## 💾 Memoria NOR Flash & Sistema de Archivos SPIFFS
El particionamiento se gestiona a través de una tabla personalizada (partitions.csv) optimizada para 4 MB de memoria Flash:

nvs (24 KB - 0x9000): Almacenamiento no volátil de parámetros RF, calibraciones PHY y datos de bonding BLE.

factory (1.93 MB - 0x10000): Imagen binaria de la aplicación compilada en C nativo.

spiffs (1 MB - 0x200000): Partición formateada como sistema de archivos virtual (VFS) que contiene:

/spiffs/wifi_config.txt: Credenciales de red persistentes en formato clave-valor (SSID=..., PASS=...).

/spiffs/index.html: Estructura del Portal Cautivo y panel de telemetría.

/spiffs/style.css: Estilos visuales optimizados para navegadores móviles.

## ⚙️ Configuración Parametrizada (Kconfig)
Para evitar la mala práctica de incrustar contraseñas en el código fuente (hardcoded secrets), el proyecto incorpora un menú de configuración de Kconfig (main/Kconfig.projbuild):

CONFIG_AP_WIFI_PASSWORD: Contraseña WPA2-PSK por defecto para la red SoftAP de rescate (mínimo 8 caracteres para cumplir con el estándar 802.11i).

CONFIG_HTTPD_MAX_REQ_HDR_LEN: Ampliado a 2048 bytes en sdkconfig.defaults para evitar errores de truncado 431 Request Header Fields Too Large ante cabeceras emitidas por navegadores modernos.

## 🛠️ Estructura del Repositorio
```text
.
├── CMakeLists.txt                  # Configuración raíz de CMake
├── sdkconfig.defaults              # Parámetros base (Partición personalizada + 4MB Flash + NimBLE)
├── partitions.csv                  # Tabla de particiones (1MB SPIFFS / 1.93MB App)
├── README.md                       # Documentación técnica del sistema
├── components/                     # Componentes externos desacoplados
│   └── ssd1306/                    # Driver I2C para pantalla OLED SSD1306
├── spiffs/                         # Datos crudos empaquetados en la Flash
│   ├── index.html                  # Portal Cautivo y Dashboard de Telemetría
│   ├── style.css                   # Hoja de estilos CSS
│   └── wifi_config.txt             # Archivo de configuración inicial
└── main/
    ├── CMakeLists.txt              # Registro de fuentes, dependencias y spiffs_create_partition_image
    ├── Kconfig.projbuild           # Menú interactivo de configuración (menuconfig)
    ├── main.c                      # Punto de entrada (app_main), IPC y FSM
    ├── include/
    │   ├── adc_sampler.h           # Muestreo ADC1_CH6 Oneshot + eFuse
    │   ├── ble_prov.h              # Servicio GATT BLE bajo NimBLE
    │   ├── display_7seg.h          # Driver 7-Seg Ánodo Común (GPIO directo)
    │   ├── oled_display.h          # Renderizado de métricas en SSD1306
    │   ├── spiffs_manager.h        # Acceso VFS a SPIFFS con protección de concurrencia
    │   ├── web_server.h            # API REST HTTP y servidor de archivos estáticos
    │   └── wifi_manager.h          # Máquina de estados Wi-Fi (STA / SoftAP)
    └── src/
        ├── adc_sampler.c
        ├── ble_prov.c
        ├── display_7seg.c
        ├── oled_display.c
        ├── spiffs_manager.c
        ├── web_server.c
        └── wifi_manager.c
```

## 🚀 Instrucciones de Compilación y Grabación
1. Cargar el entorno de desarrollo ESP-IDF (v5.3.1):
```text
. $HOME/esp/esp-idf/export.sh
# O el alias configurado:
get_idf
```

2. Personalizar parámetros del sistema (Opcional):
```text
idf.py menuconfig
```
(Navegar a Configuracion Wi-Fi SBC para ajustar la contraseña de rescate por defecto).

3. Compilar el proyecto y generar la imagen SPIFFS:
```text
idf.py build
```

4. Flashear la aplicación junto con la tabla de particiones y los datos de SPIFFS:
```text
idf.py -p /dev/ttyUSB0 monitor
```

## 🧪 Protocolo de Pruebas y Validación en Laboratorio
Prueba A: Modo Operativo Normal (Conectado a Infraestructura STA)
1. Definir credenciales de un router local o punto de acceso móvil en spiffs/wifi_config.txt.
2. Flashear y arrancar la placa (idf.py flash monitor).
3. Comportamiento esperado:
  * La consola registra IP_EVENT_STA_GOT_IP con la dirección asignada por el router.
  * La pantalla OLED muestra la IP del dispositivo, la lectura en Luxes y la barra de nivel.
  * El display de 7 segmentos varía de 0 a 9 proporcionalmente a la luz incidente en la LDR.
  * Conectando un ordenador a la misma subred, la interfaz web es accesible en http://<IP_ASIGNADA>.

## Prueba B: Tolerancia a Fallos y Conmutación a SoftAP (Failover)
1. Apagar el router o escribir credenciales inválidas en spiffs/wifi_config.txt.
2. Reiniciar el microcontrolador.
3. Comportamiento esperado:
    * La FSM ejecuta 5 reintentos de conexión antes de abortar el modo estación.
    * Se detiene la interfaz STA y se levanta el SoftAP con SSID ESP32_XXXX (donde XXXX son los últimos 2 bytes de la MAC).
    * La IP estática del Gateway se fija deterministamente en 192.168.10.1.
    * Conectar un smartphone o PC a la red Wi-Fi ESP32_XXXX (Contraseña configurada en Kconfig).
    * Acceder desde el navegador a http://192.168.10.1 para cargar el Portal Cautivo.
    * Enviar nuevas credenciales válidas: el servidor las escribe en /spiffs/wifi_config.txt y reinicia el sistema para volver a la Prueba A de forma autónoma.

## Prueba C: Canal de Provisión Secundario por Bluetooth (NimBLE)
1. Mientras el dispositivo se encuentre en estado de Failover, abrir una aplicación de escaneo BLE (nRF Connect o LightBlue).
2. Localizar el periférico anunciado con el nombre ESP32_BLE_PROV.
3. Conectar e inspeccionar el servicio GATT de provisión para verificar la lectura/escritura de credenciales Wi-Fi mediante características BLE.

## 📦 Checklist de Control de Entregas (Normativa UPM)
* [ ] Limpieza de Binarios: Ejecutar idf.py fullclean antes de empaquetar el archivo ZIP final para eliminar la carpeta build/ y cumplir los límites de tamaño.
* [ ] Saneamiento de Git: Eliminar directorios .git internos dentro de components/ssd1306/ para prevenir errores de submódulos huérfanos al descomprimir.
* [ ] Vídeo Demostrativo (Máximo 3 minutos):
    * Demostración del arranque del firmware y conmutación automática a modo Failover.
    * Conexión del smartphone al SoftAP y acceso al Portal Cautivo (192.168.10.1).
    * Demostración de respuesta analógica variando la luz sobre la LDR y observando la sincronización entre el display OLED y el display de 7 segmentos.
* [ ] Permisos del Enlace (SharePoint / OneDrive UPM): Comprobar que el enlace compartido esté configurado como "Cualquier persona con el enlace puede ver" para garantizar la evaluación por parte del tribunal sin incidencias de acceso.