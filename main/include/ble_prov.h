#ifndef BLE_PROV_H
#define BLE_PROV_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa la pila Bluetooth NimBLE, registra el perfil GATT de provisión
 *        y comienza los anuncios (Advertisements) BLE.
 * 
 * @return esp_err_t ESP_OK si se completó la inicialización con éxito.
 */
esp_err_t ble_prov_init(void);

/**
 * @brief Detiene el stack NimBLE y la radio Bluetooth.
 */
void ble_prov_stop(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROV_H