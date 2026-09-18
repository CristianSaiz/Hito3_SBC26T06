#include "ble_prov.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spiffs_manager.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_PROV";

static uint8_t s_ble_addr_type;
static char s_temp_ssid[MAX_SSID_LEN + 1] = {0};
static char s_temp_pass[MAX_PASS_LEN + 1] = {0};

// UUIDs de 16 bits para Servicio (0xFF00) y Caracteristicas (0xFF01 = SSID, 0xFF02 = PASS)
static const ble_uuid16_t gatt_svc_uuid = BLE_UUID16_INIT(0xFF00);
static const ble_uuid16_t gatt_char_ssid_uuid = BLE_UUID16_INIT(0xFF01);
static const ble_uuid16_t gatt_char_pass_uuid = BLE_UUID16_INIT(0xFF02);

static void restart_task(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGW(TAG, "Reiniciando sistema tras recibir credenciales via BLE...");
    esp_restart();
}

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (uuid16 == 0xFF01) { // SSID
            int len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > MAX_SSID_LEN) len = MAX_SSID_LEN;
            ble_hs_mbuf_to_flat(ctxt->om, s_temp_ssid, len, NULL);
            s_temp_ssid[len] = '\0';
            ESP_LOGI(TAG, "SSID recibido via BLE: %s", s_temp_ssid);
        } else if (uuid16 == 0xFF02) { // PASS
            int len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > MAX_PASS_LEN) len = MAX_PASS_LEN;
            ble_hs_mbuf_to_flat(ctxt->om, s_temp_pass, len, NULL);
            s_temp_pass[len] = '\0';
            ESP_LOGI(TAG, "PASS recibida via BLE. Guardando en SPIFFS...");

            if (strlen(s_temp_ssid) > 0 && strlen(s_temp_pass) > 0) {
                if (spiffs_write_wifi_credentials(s_temp_ssid, s_temp_pass) == ESP_OK) {
                    xTaskCreate(restart_task, "ble_restart_task", 2048, NULL, 5, NULL);
                }
            }
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_char_ssid_uuid.u,
                .access_cb = gatt_svr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &gatt_char_pass_uuid.u,
                .access_cb = gatt_svr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },
    { 0 }
};

static void ble_prov_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ESP32_BLE_PROV";
    fields.name_len = strlen("ESP32_BLE_PROV");
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error estableciendo campos de anuncio BLE: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error iniciando anuncios BLE: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "Anuncios BLE iniciados correctamente (SSID: ESP32_BLE_PROV)");
}

static void ble_prov_on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_ble_addr_type);
    ble_prov_advertise();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_prov_init(void)
{
    ESP_LOGI(TAG, "Inicializando stack Bluetooth NimBLE...");

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Fallo al inicializar nimble_port: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = ble_prov_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }

    ble_svc_gap_device_name_set("ESP32_BLE_PROV");
    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

void ble_prov_stop(void)
{
    nimble_port_stop();
    nimble_port_deinit();
    ESP_LOGI(TAG, "Stack BLE detenido correctamente");
}
