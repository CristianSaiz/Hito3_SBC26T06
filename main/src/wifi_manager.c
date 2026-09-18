#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h" // Cabecera necesaria para la macro IP4_ADDR
#include "sdkconfig.h"

static const char *TAG = "WIFI_MGR";

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_sta_active = false; // true solo mientras STA debe intentar conectar

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_sta_active) {
            esp_wifi_connect();
        }
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_sta_active) {
            // Ya estamos en failover (APSTA); ignorar desconexiones fantasma
            return;
        }
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reintento de conexion a la red (%d/5)...", s_retry_num);
        } else {
            ESP_LOGW(TAG, "Fallo persistente de conexion STA. Reintentos agotados (5).");
            // Notificar a la máquina de estados de main.c
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado exitosamente. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_retry_num = 0;

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            ESP_LOGE(TAG, "Error al crear el Event Group de Wi-Fi");
            return ESP_FAIL;
        }
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // 1. Inicializar LwIP y el bucle de eventos por defecto
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    // 2. Guarda estática: Crear la interfaz STA una sola vez
    static bool s_sta_initialized = false;

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    // 3. Registrar eventos e inicializar el driver Wi-Fi solo en el primer arranque
    if (!s_sta_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL
        ));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL
        ));

        s_sta_initialized = true;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_sta_active = true; // <-- STA activo: el handler puede intentar conectar/reconectar

    ESP_LOGI(TAG, "wifi_init_sta completado. Configurado para SSID: %s", ssid);
    return ESP_OK;
}

EventBits_t wifi_wait_for_sta_result(uint32_t timeout_ms)
{
    if (s_wifi_event_group == NULL) {
        return 0;
    }

    return xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );
}

void wifi_stop_sta(void)
{
    s_sta_active = false; // <-- deja de intentar conectar/reconectar antes de detener el driver

    if (s_sta_netif != NULL) {
        // Silenciar temporalmente el tag "wifi": esp_wifi_stop() emite un
        // mensaje benigno ("NAN WiFi stop") con nivel ERROR que no indica
        // ningún fallo real, y que es imposible de filtrar por severidad
        // porque ESP_LOG_WARN también deja pasar ESP_LOG_ERROR.
        esp_log_level_set("wifi", ESP_LOG_NONE);
        esp_wifi_stop();
        esp_log_level_set("wifi", ESP_LOG_INFO); // restaurar nivel normal
        
        ESP_LOGI(TAG, "Driver Wi-Fi STA detenido correctamente (driver en RAM listo para AP)");
    }
}

esp_err_t wifi_init_softap(const char *ap_pass)
{
    static bool s_softap_active = false;

    if (s_softap_active) {
        ESP_LOGW(TAG, "SoftAP ya activo. Ignorando peticion duplicada.");
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    char ap_ssid[32] = {0};
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32_%02X%02X", mac[4], mac[5]);

    const char *pass_to_use = (ap_pass && strlen(ap_pass) >= 8) ? ap_pass : CONFIG_DEFAULT_AP_PASSWORD;

    // Crear la interfaz LwIP solo si no existe (usa la variable global s_ap_netif)
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();

        ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));

        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
        IP4_ADDR(&ip_info.ip, 192, 168, 10, 1);
        IP4_ADDR(&ip_info.gw, 192, 168, 10, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    strncpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, pass_to_use, sizeof(wifi_config.ap.password));

    // Cambiar a APSTA para permitir escaneo simultáneo
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_softap_active = true;

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "📡 SoftAP Seguro (APSTA) Iniciado Correctamente");
    ESP_LOGI(TAG, " ➔ SSID Broadcast:  %s", ap_ssid);
    ESP_LOGI(TAG, " ➔ Clave WPA2:       %s", pass_to_use);
    ESP_LOGI(TAG, " ➔ IP Gateway AP:   192.168.10.1");
    ESP_LOGI(TAG, "==================================================");

    return ESP_OK;
}

void wifi_get_ip_string(char *ip_str, size_t max_len)
{
    if (ip_str == NULL || max_len == 0) return;

    esp_netif_ip_info_t ip_info;
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return;
    }

    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return;
    }

    snprintf(ip_str, max_len, "0.0.0.0");
}