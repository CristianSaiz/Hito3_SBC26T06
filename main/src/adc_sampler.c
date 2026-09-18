#include "adc_sampler.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC_SAMPLER";

#define LDR_ADC_UNIT        ADC_UNIT_1
#define LDR_ADC_CHANNEL     ADC_CHANNEL_6   // GPIO 34 (Inmune a interferencias Wi-Fi)
#define LDR_ADC_ATTEN       ADC_ATTEN_DB_12 // Rango dinamico completo 0 - 3.3V
#define NO_OF_SAMPLES       16              // Oversampling para filtrado de ruido

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_do_calibration = false;

esp_err_t adc_sampler_init(void)
{
    ESP_LOGI(TAG, "Inicializando ADC1_CH6 (GPIO 34) en modo Oneshot con calibracion eFuse...");

    // 1. Instanciacion del controlador ADC1 Oneshot
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = LDR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    // 2. Configuracion del canal analógico en GPIO 34 (12 bits)
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = LDR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, LDR_ADC_CHANNEL, &chan_config));

    // 3. Compensacion eFuse Line-Fitting
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = LDR_ADC_UNIT,
        .atten = LDR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_do_calibration = true;
        ESP_LOGI(TAG, "Calibracion Line Fitting eFuse cargada con exito.");
    } else {
        ESP_LOGW(TAG, "eFuse sin calibrar de fabrica; operando con aproximacion teorica.");
    }
#endif

    return ESP_OK;
}

esp_err_t adc_sampler_read(ldr_telemetry_t *data)
{
    if (data == NULL || !s_adc_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t adc_reading = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, LDR_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error en lectura ADC Oneshot: %s", esp_err_to_name(ret));
            return ret;
        }
        adc_reading += raw;
    }
    adc_reading /= NO_OF_SAMPLES;

    data->raw_val = adc_reading;

    int voltage_mv = 0;
    if (s_do_calibration) {
        if (adc_cali_raw_to_voltage(s_cali_handle, (int)adc_reading, &voltage_mv) == ESP_OK) {
            data->voltage_mv = (uint32_t)voltage_mv;
        } else {
            data->voltage_mv = (adc_reading * 3300) / 4095;
        }
    } else {
        data->voltage_mv = (adc_reading * 3300) / 4095;
    }

    // Calculo aproximado de luxes segun curva LDR + R_pull_down (10k)
    data->lux = (data->voltage_mv * 2) / 10;

    return ESP_OK;
}