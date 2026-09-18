#ifndef ADC_SAMPLER_H
#define ADC_SAMPLER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Estructura de datos para la telemetria LDR
typedef struct {
    uint32_t raw_val;      // Valor bruto ADC (0-4095)
    uint32_t voltage_mv;   // Tension calibrada por eFuse en mV
    uint32_t lux;          // Estimacion de iluminacion en Lux
} ldr_telemetry_t;

/**
 * @brief Inicializa el atenuador y la unidad ADC1_CH6 (GPIO 34) con calibracion eFuse Line-Fitting.
 * 
 * @return esp_err_t ESP_OK si la inicializacion fue correcta.
 */
esp_err_t adc_sampler_init(void);

/**
 * @brief Obtiene una muestra promediada del sensor LDR y calcula la tension y luxes.
 * 
 * @param[out] data Puntero a la estructura donde se almacenara el resultado.
 * @return esp_err_t ESP_OK si la lectura fue exitosa.
 */
esp_err_t adc_sampler_read(ldr_telemetry_t *data);

#ifdef __cplusplus
}
#endif

#endif // ADC_SAMPLER_H