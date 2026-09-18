#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Longitudes máximas estándar para redes Wi-Fi WPA2
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

/**
 * @brief Inicializa y monta el sistema de archivos VFS SPIFFS en la partición 'spiffs'.
 *        También inicializa el mutex de protección para accesos multihilo.
 * 
 * @return esp_err_t ESP_OK si se montó correctamente, ESP_FAIL en caso de error.
 */
esp_err_t spiffs_init(void);

/**
 * @brief Lee las credenciales de Wi-Fi desde el fichero plano '/spiffs/wifi_config.txt'.
 * 
 * @param[out] ssid Buffer donde se copiará el SSID leído (mínimo 33 bytes).
 * @param[out] pass Buffer donde se copiará la contraseña leída (mínimo 65 bytes).
 * @return esp_err_t ESP_OK si se leyó correctamente, ESP_ERR_NOT_FOUND si no existe el fichero.
 */
esp_err_t spiffs_read_wifi_credentials(char *ssid, char *pass);

/**
 * @brief Sobreescribe atómicamente el fichero '/spiffs/wifi_config.txt' con nuevas credenciales.
 * 
 * @param[in] ssid Nuevo SSID a guardar.
 * @param[in] pass Nueva contraseña a guardar.
 * @return esp_err_t ESP_OK si se guardó con éxito, ESP_FAIL si hubo error de escritura.
 */
esp_err_t spiffs_write_wifi_credentials(const char *ssid, const char *pass);

/**
 * @brief Desmonta el sistema de archivos SPIFFS de la memoria Flash.
 */
void spiffs_deinit(void);

/**
 * @brief Lee la contraseña deseada para el SoftAP desde /spiffs/wifi_config.txt (clave AP_PASS).
 * 
 * @param[out] ap_pass Buffer de salida donde se guardará la contraseña del SoftAP.
 * @return esp_err_t ESP_OK si se leyó con éxito.
 */
esp_err_t spiffs_read_ap_password(char *ap_pass);

#ifdef __cplusplus
}
#endif

#endif // SPIFFS_MANAGER_H