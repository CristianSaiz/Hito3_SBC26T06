#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include "spiffs_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ble_prov.h"
#include "adc_sampler.h"
#include "display_7seg.h"
#include "oled_display.h"

static const char *TAG = "MAIN_APP";

// Cola de comunicación Inter-Tarea (IPC) para telemetría LDR
static QueueHandle_t s_telemetry_queue = NULL;

/**
 * @brief Tarea Productora: Muestrea el ADC1_CH6 (GPIO 34) periódicamente y envía a la cola.
 *        Asignada al Core 1 (APP_CPU).
 */
static void adc_task(void *pvParameters)
{
    ldr_telemetry_t data;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(500);

    ESP_LOGI(TAG, "Tarea ADC iniciada en Core %d", xPortGetCoreID());

    for (;;) {
        if (adc_sampler_read(&data) == ESP_OK) {
            // Enviar a la cola IPC (sobrescribe si está llena para garantizar dato fresco)
            xQueueOverwrite(s_telemetry_queue, &data);
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief Tarea Consumidora: Recibe muestras de la cola y actualiza OLED y 7-Segmentos.
 *        Asignada al Core 1 (APP_CPU).
 */
static void display_task(void *pvParameters)
{
    ldr_telemetry_t data;
    char ip_buffer[16] = {0};
    ESP_LOGI(TAG, "Tarea Display iniciada en Core %d", xPortGetCoreID());

    for (;;) {
        if (xQueueReceive(s_telemetry_queue, &data, portMAX_DELAY) == pdTRUE) {
            // 1. Escalado a dígito (0-9) para Display 7 Segmentos
            uint8_t digit_to_show = (uint8_t)((data.raw_val * 10) / 4096);
            if (digit_to_show > 9) digit_to_show = 9;
            display_7seg_show_digit(digit_to_show);

            // 2. Obtener IP activa del adaptador de red (STA o SoftAP)
            wifi_get_ip_string(ip_buffer, sizeof(ip_buffer));

            // 3. Actualizar OLED con telemetría e IP
            oled_display_update(ip_buffer, data.lux, data.raw_val);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== INICIANDO SISTEMA EMBEBIDO HITO 3 (SBC26T06) ===");

    // 1. Inicializar almacenamiento persistente NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Montar Sistema de Archivos SPIFFS
    ESP_ERROR_CHECK(spiffs_init());

    // 3. Inicializar Periféricos de Hardware (ADC1, 7-Seg, OLED I2C)
    ESP_ERROR_CHECK(adc_sampler_init());
    ESP_ERROR_CHECK(display_7seg_init());
    ESP_ERROR_CHECK(oled_display_init());

    // 4. Crear Cola IPC para telemetría (Longitud = 1 con sobreescritura)
    s_telemetry_queue = xQueueCreate(1, sizeof(ldr_telemetry_t));
    if (s_telemetry_queue == NULL) {
        ESP_LOGE(TAG, "Error crítico al instanciar cola IPC");
        return;
    }

    // 5. Crear tareas de instrumentación asignadas estrictamente al Core 1 (APP_CPU)
    xTaskCreatePinnedToCore(adc_task, "adc_task", 3072, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display_task", 3072, NULL, 4, NULL, 1);

    // 6. Cargar credenciales STA desde /spiffs/wifi_config.txt con fallback seguro
    char ssid[MAX_SSID_LEN + 1] = {0};
    char pass[MAX_PASS_LEN + 1] = {0};

    if (spiffs_read_wifi_credentials(ssid, pass) != ESP_OK) {
        ESP_LOGW(TAG, "Fichero no encontrado. Usando credenciales por defecto...");
        strncpy(ssid, "SBC", sizeof(ssid) - 1);
        strncpy(pass, "SBCwifi$", sizeof(pass) - 1);
    }

    // 7. Iniciar intento de conexión STA en Core 0 (PRO_CPU)
    ESP_LOGI(TAG, "Intentando conexión STA a SSID: %s", ssid);
    ESP_ERROR_CHECK(wifi_init_sta(ssid, pass));

    // Bloqueo sincronizado hasta resolución de IP o fallo de reintentos
    EventBits_t bits = wifi_wait_for_sta_result(15000);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, ">>> ESTADO: OPERATIVO NORMAL (STA Conectado)   <<<");
        ESP_LOGI(TAG, "==================================================");

        // Levantar servidor web para monitorización de telemetría en red local
        start_webserver();
    } else {
        ESP_LOGW(TAG, "==================================================");
        ESP_LOGW(TAG, ">>> ESTADO: FAILOVER (SoftAP + HTTP + BLE)     <<<");
        ESP_LOGW(TAG, "==================================================");

        // 1. Cargar contraseña de SoftAP desde SPIFFS con fallback estricto a Kconfig
        char ap_pass[MAX_PASS_LEN + 1] = {0};
        if (spiffs_read_ap_password(ap_pass) != ESP_OK || strlen(ap_pass) < 8) {
            ESP_LOGW(TAG, "Contraseña AP en SPIFFS ausente o < 8 caracteres. Aplicando Kconfig.");
            #ifdef CONFIG_AP_WIFI_PASSWORD
                strncpy(ap_pass, CONFIG_AP_WIFI_PASSWORD, sizeof(ap_pass) - 1);
            #elif defined(CONFIG_DEFAULT_AP_PASSWORD)
                strncpy(ap_pass, CONFIG_DEFAULT_AP_PASSWORD, sizeof(ap_pass) - 1);
            #else
                strncpy(ap_pass, "12345678", sizeof(ap_pass) - 1);
            #endif
        }

        // 2. Detener interfaz STA fallida e iniciar SoftAP (192.168.10.1)
        wifi_stop_sta();
        ESP_ERROR_CHECK(wifi_init_softap(ap_pass));

        // 3. Levantar servicios de aprovisionamiento y configuración
        start_webserver();
        ESP_ERROR_CHECK(ble_prov_init());
    }
}