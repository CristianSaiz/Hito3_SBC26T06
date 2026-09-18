#include "spiffs_manager.h"
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "SPIFFS_MGR";

#define CONFIG_FILE_PATH "/spiffs/wifi_config.txt"

// Semáforo Mutex para acceso seguro multihilo a SPIFFS (Servidor Web / BLE / Main)
static SemaphoreHandle_t s_spiffs_mutex = NULL;

esp_err_t spiffs_init(void)
{
    ESP_LOGI(TAG, "Inicializando sistema de archivos VFS SPIFFS...");

    // Crear el Mutex si no ha sido creado previamente
    if (s_spiffs_mutex == NULL) {
        s_spiffs_mutex = xSemaphoreCreateMutex();
        if (s_spiffs_mutex == NULL) {
            ESP_LOGE(TAG, "Error al crear el Mutex de SPIFFS");
            return ESP_FAIL;
        }
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Fallo al montar o formatear la particion SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "No se encontro la particion 'spiffs' en partitions.csv");
        } else {
            ESP_LOGE(TAG, "Error al inicializar SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS montado con exito. Total: %d bytes, Usado: %d bytes", total, used);
    } else {
        ESP_LOGE(TAG, "Error al obtener informacion de SPIFFS (%s)", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t spiffs_read_wifi_credentials(char *ssid, char *pass)
{
    if (ssid == NULL || pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Proteger el acceso al sistema de archivos mediante Mutex
    if (xSemaphoreTake(s_spiffs_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout al intentar obtener el Mutex para lectura");
        return ESP_ERR_TIMEOUT;
    }

    FILE *f = fopen(CONFIG_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Fichero de configuracion '%s' no existe", CONFIG_FILE_PATH);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    char line[128];
    ssid[0] = '\0';
    pass[0] = '\0';

    while (fgets(line, sizeof(line), f) != NULL) {
        // Eliminar saltos de linea '\r' y '\n'
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "SSID=", 5) == 0) {
            strncpy(ssid, line + 5, MAX_SSID_LEN);
            ssid[MAX_SSID_LEN] = '\0';
        } else if (strncmp(line, "PASS=", 5) == 0) {
            strncpy(pass, line + 5, MAX_PASS_LEN);
            pass[MAX_PASS_LEN] = '\0';
        }
    }

    fclose(f);
    xSemaphoreGive(s_spiffs_mutex);

    if (strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Credenciales leidas de SPIFFS -> SSID: %s", ssid);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Fichero leido pero sin formato valido SSID=");
    return ESP_FAIL;
}

esp_err_t spiffs_write_wifi_credentials(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_spiffs_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout al intentar obtener el Mutex para escritura");
        return ESP_ERR_TIMEOUT;
    }

    FILE *f = fopen(CONFIG_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Error al abrir '%s' para escritura", CONFIG_FILE_PATH);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    fprintf(f, "SSID=%s\n", ssid);
    fprintf(f, "PASS=%s\n", pass);

    fclose(f);
    xSemaphoreGive(s_spiffs_mutex);

    ESP_LOGI(TAG, "Nuevas credenciales guardadas en SPIFFS -> SSID: %s", ssid);
    return ESP_OK;
}

void spiffs_deinit(void)
{
    esp_vfs_spiffs_unregister("spiffs");
    if (s_spiffs_mutex != NULL) {
        vSemaphoreDelete(s_spiffs_mutex);
        s_spiffs_mutex = NULL;
    }
    ESP_LOGI(TAG, "SPIFFS desmontado correctamente");
}

esp_err_t spiffs_read_ap_password(char *ap_pass)
{
    if (ap_pass == NULL) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout esperando el Mutex de SPIFFS");
        return ESP_ERR_TIMEOUT;
    }

    FILE *f = fopen("/spiffs/wifi_config.txt", "r");
    if (f == NULL) {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    char line[128];
    bool found = false;
    ap_pass[0] = '\0';

    while (fgets(line, sizeof(line), f) != NULL) {
        // Eliminar saltos de linea
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "AP_PASS=", 8) == 0) {
            strncpy(ap_pass, line + 8, MAX_PASS_LEN);
            ap_pass[MAX_PASS_LEN] = '\0';
            found = true;
            break;
        }
    }

    fclose(f);
    xSemaphoreGive(s_spiffs_mutex);

    if (found && strlen(ap_pass) >= 8) {
        ESP_LOGI(TAG, "Contraseña de SoftAP leida de SPIFFS correctamente");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "AP_PASS no encontrado o invalido en SPIFFS. Usando clave por defecto.");
    strncpy(ap_pass, "SBC26T06$", MAX_PASS_LEN);
    return ESP_OK;
}