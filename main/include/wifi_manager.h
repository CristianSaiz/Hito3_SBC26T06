#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXIMUM_RETRY              5
#define WIFI_CONNECTED_BIT         BIT0
#define WIFI_FAIL_BIT              BIT1

// Clave por defecto de fábrica (Factory Fallback) ante fallos o ausencia en SPIFFS
#define CONFIG_DEFAULT_AP_PASSWORD "SBC26T06$"

/**
 * @brief Inicializa la interfaz Wi-Fi en modo Estacion (STA).
 * 
 * @param[in] ssid Nombre de la red Wi-Fi.
 * @param[in] pass Contraseña WPA2 de la red.
 * @return esp_err_t ESP_OK si la inicialización fue correcta.
 */
esp_err_t wifi_init_sta(const char *ssid, const char *pass);

/**
 * @brief Espera hasta que la conexion STA se establezca o se agoten los reintentos.
 * 
 * @param[in] timeout_ms Tiempo maximo de espera en milisegundos.
 * @return EventBits_t Bits de evento resultantes.
 */
EventBits_t wifi_wait_for_sta_result(uint32_t timeout_ms);

/**
 * @brief Detiene la radio Wi-Fi STA conservando el driver en RAM listo para SoftAP.
 */
void wifi_stop_sta(void);

/**
 * @brief Inicializa la interfaz Wi-Fi en modo SoftAP con seguridad WPA2-PSK y subred personalizada LwIP.
 * 
 * @param[in] ap_pass Contraseña para la red SoftAP (mínimo 8 caracteres).
 * @return esp_err_t ESP_OK si la inicialización fue correcta.
 */
esp_err_t wifi_init_softap(const char *ap_pass);

/**
 * @brief Obtiene la direccion IP actual (STA o AP) formateada en texto.
 * 
 * @param[out] ip_str Buffer destino para almacenar la IP.
 * @param[in] max_len Tama~no maximo del buffer.
 */
void wifi_get_ip_string(char *ip_str, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H