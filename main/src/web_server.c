#include "web_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "spiffs_manager.h"

static const char *TAG = "WEB_SERVER";

// ---------------------------------------------------------------------
// Infraestructura para el escaneo Wi-Fi en tarea dedicada (evita stack
// overflow: esp_wifi_scan_start bloqueante consume mucho stack y la
// tarea del servidor HTTP tiene un stack pequeño por defecto)
// ---------------------------------------------------------------------
#define WIFI_SCAN_TASK_STACK_SIZE 8192
#define WIFI_SCAN_MAX_APS         10
#define WIFI_SCAN_WAIT_TIMEOUT_MS 15000

static SemaphoreHandle_t s_scan_mutex = NULL;      // evita escaneos concurrentes
static SemaphoreHandle_t s_scan_done_sem = NULL;   // señaliza fin de escaneo

static wifi_ap_record_t s_scan_ap_records[WIFI_SCAN_MAX_APS];
static uint16_t s_scan_ap_count = 0;
static esp_err_t s_scan_result_err = ESP_FAIL;

static void restart_task(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGW(TAG, "Reiniciando sistema tras actualizacion de credenciales desde el Portal Cautivo...");
    esp_restart();
}

static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a' && a <= 'f') a -= 32;
            if (a >= 'A' && a <= 'F') a = a - 'A' + 10;
            else a -= '0';
            if (b >= 'a' && b <= 'f') b -= 32;
            if (b >= 'A' && b <= 'F') b = b - 'A' + 10;
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Handler GET / (Sirve index.html desde SPIFFS)
static esp_err_t index_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Error al abrir /spiffs/index.html");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char chunk[256];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, bytes_read);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Handler GET /style.css
static esp_err_t style_css_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/style.css", "r");
    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/css");
    char chunk[256];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, bytes_read);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Handler POST /save
static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char raw_ssid[MAX_SSID_LEN * 3] = {0};
    char raw_pass[MAX_PASS_LEN * 3] = {0};
    char ssid[MAX_SSID_LEN + 1] = {0};
    char pass[MAX_PASS_LEN + 1] = {0};

    if (httpd_query_key_value(buf, "ssid", raw_ssid, sizeof(raw_ssid)) == ESP_OK &&
        httpd_query_key_value(buf, "pass", raw_pass, sizeof(raw_pass)) == ESP_OK) {
        
        url_decode(ssid, raw_ssid);
        url_decode(pass, raw_pass);

        ESP_LOGI(TAG, "Credenciales recibidas por HTTP POST -> SSID: %s", ssid);

        if (spiffs_write_wifi_credentials(ssid, pass) == ESP_OK) {
            const char *resp =
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<style>"
                "body{font-family:sans-serif;text-align:center;padding-top:50px;background:#1a1a1a;color:#eee;}"
                "h1{color:#4CAF50;}"
                ".spinner{border:4px solid #333;border-top:4px solid #4CAF50;border-radius:50%;"
                "width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;}"
                "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}"
                "</style></head><body>"
                "<h1>✅ Configuración Guardada</h1>"
                "<p>El ESP32 se está reiniciando para conectar a la nueva red...</p>"
                "<div class='spinner'></div>"
                "<p id='countdown'>Reiniciando en 15s...</p>"
                "<script>"
                "let s=15;"
                "const el=document.getElementById('countdown');"
                "setInterval(()=>{s--;if(s>=0)el.textContent='Reiniciando en '+s+'s...';"
                "if(s===0)el.textContent='Busca el dispositivo en tu nueva red Wi-Fi.';"
                "},1000);"
                "</script>"
                "</body></html>";
            httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
            xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
            return ESP_OK;
        }
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error al procesar o guardar credenciales");
    return ESP_FAIL;
}

// ---------------------------------------------------------------------
// Tarea dedicada que ejecuta el escaneo Wi-Fi bloqueante en SU PROPIO
// stack (grande), evitando así desbordar el stack del servidor HTTP.
// ---------------------------------------------------------------------
static void wifi_scan_task(void *pvParameters)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI(TAG, "Iniciando escaneo Wi-Fi en modo APSTA (tarea dedicada)...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true); // bloqueante, stack propio

    s_scan_result_err = err;
    s_scan_ap_count = 0;

    if (err == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        uint16_t number = WIFI_SCAN_MAX_APS;
        memset(s_scan_ap_records, 0, sizeof(s_scan_ap_records));

        if (esp_wifi_scan_get_ap_records(&number, s_scan_ap_records) == ESP_OK) {
            s_scan_ap_count = (number < ap_count) ? number : ap_count;
        }
    } else {
        ESP_LOGE(TAG, "Fallo al ejecutar esp_wifi_scan_start: 0x%x", err);
    }

    xSemaphoreGive(s_scan_done_sem); // avisa al handler HTTP que ya puede leer resultados
    vTaskDelete(NULL);
}

// Handler GET /scan
// Sigue siendo síncrono de cara al frontend: crea la tarea de escaneo,
// espera (bloqueado en semáforo, consumo de stack mínimo) a que termine,
// y construye el mismo JSON que antes.
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
    }
    if (s_scan_done_sem == NULL) {
        s_scan_done_sem = xSemaphoreCreateBinary();
    }

    // Evita escaneos concurrentes si llegan dos peticiones a la vez
    if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Escaneo ya en curso, intenta de nuevo");
        return ESP_FAIL;
    }

    BaseType_t created = xTaskCreate(
        wifi_scan_task,
        "wifi_scan_task",
        WIFI_SCAN_TASK_STACK_SIZE,
        NULL,
        5,
        NULL
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea de escaneo Wi-Fi (memoria insuficiente?)");
        xSemaphoreGive(s_scan_mutex);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo iniciar el escaneo");
        return ESP_FAIL;
    }

    // Espera bloqueada (barata en stack) a que la tarea termine el escaneo
    if (xSemaphoreTake(s_scan_done_sem, pdMS_TO_TICKS(WIFI_SCAN_WAIT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout esperando el resultado del escaneo Wi-Fi");
        xSemaphoreGive(s_scan_mutex);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Timeout en el escaneo Wi-Fi");
        return ESP_FAIL;
    }

    char resp_json[1024];
    int len = snprintf(resp_json, sizeof(resp_json), "[");

    if (s_scan_result_err == ESP_OK) {
        for (int i = 0; i < s_scan_ap_count; i++) {
            if (strlen((char *)s_scan_ap_records[i].ssid) == 0) continue;
            len += snprintf(resp_json + len, sizeof(resp_json) - len,
                            "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                            (i > 0 && len > 1) ? "," : "",
                            (char *)s_scan_ap_records[i].ssid,
                            s_scan_ap_records[i].rssi);
        }
    }

    snprintf(resp_json + len, sizeof(resp_json) - len, "]");

    xSemaphoreGive(s_scan_mutex);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Aumentar el búfer de cabeceras HTTP a 1024 bytes para soportar navegadores móviles
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Arrancando Servidor Web HTTP en puerto: %d (Max HDR: %d)", 
             config.server_port, CONFIG_HTTPD_MAX_REQ_HDR_LEN);

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t style_uri = {
            .uri       = "/style.css",
            .method    = HTTP_GET,
            .handler   = style_css_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &style_uri);

        httpd_uri_t save_uri = {
            .uri       = "/save",
            .method    = HTTP_POST,
            .handler   = save_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &save_uri);

        httpd_uri_t scan_uri = {
            .uri       = "/scan",
            .method    = HTTP_GET,
            .handler   = scan_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);

        return server;
    }

    ESP_LOGE(TAG, "Error al arrancar el Servidor Web HTTP");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "Servidor Web HTTP detenido");
    }
}