#ifndef DISPLAY_7SEG_H
#define DISPLAY_7SEG_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa los 7 GPIOs configurados para los segmentos A-G del display 5161BS.
 */
esp_err_t display_7seg_init(void);

/**
 * @brief Muestra un dígito numérico (0-9) en el display de 7 segmentos.
 * 
 * @param[in] number Dígito a representar.
 */
void display_7seg_show_digit(uint8_t number);

/**
 * @brief Apaga todos los segmentos del display.
 */
void display_7seg_clear(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_7SEG_H