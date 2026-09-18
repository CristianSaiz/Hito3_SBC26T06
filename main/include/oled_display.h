#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_display_init(void);

/**
 * @brief Actualiza la pantalla OLED con la IP real, Luxes cualitativos, Porcentaje y Barra Grafica.
 * 
 * @param[in] ip_str Cadena con la direccion IP real.
 * @param[in] lux Valor de iluminacion en Luxes.
 * @param[in] raw_val Valor bruto ADC (0-4095) para el calculo del porcentaje.
 */
void oled_display_update(const char *ip_str, uint32_t lux, uint32_t raw_val);

#ifdef __cplusplus
}
#endif

#endif // OLED_DISPLAY_H