#include "display_7seg.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "7SEG";

// Mapeo de GPIOs segun arquitectura del Hito 2 (Segmentos A, B, C, D, E, F, G)
static const gpio_num_t SEGMENT_PINS[7] = {
    GPIO_NUM_13, // Segmento A
    GPIO_NUM_12, // Segmento B
    GPIO_NUM_14, // Segmento C
    GPIO_NUM_27, // Segmento D
    GPIO_NUM_26, // Segmento E
    GPIO_NUM_25, // Segmento F
    GPIO_NUM_33  // Segmento G
};

// Tabla de verdad para Ánodo Común (0 = Encendido / Activo por masa)
static const uint8_t DIGIT_MAP[10][7] = {
    {0, 0, 0, 0, 0, 0, 1}, // 0
    {1, 0, 0, 1, 1, 1, 1}, // 1
    {0, 0, 1, 0, 0, 1, 0}, // 2
    {0, 0, 0, 0, 1, 1, 0}, // 3
    {1, 0, 0, 1, 1, 0, 0}, // 4
    {0, 1, 0, 0, 1, 0, 0}, // 5
    {0, 1, 0, 0, 0, 0, 0}, // 6
    {0, 0, 0, 1, 1, 1, 1}, // 7
    {0, 0, 0, 0, 0, 0, 0}, // 8
    {0, 0, 0, 0, 1, 0, 0}  // 9
};

esp_err_t display_7seg_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    for (int i = 0; i < 7; i++) {
        io_conf.pin_bit_mask |= (1ULL << SEGMENT_PINS[i]);
    }

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    display_7seg_clear();
    ESP_LOGI(TAG, "Display de 7 segmentos inicializado correctamente");
    return ESP_OK;
}

void display_7seg_show_digit(uint8_t number)
{
    if (number > 9) return;
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEGMENT_PINS[i], DIGIT_MAP[number][i]);
    }
}

void display_7seg_clear(void)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEGMENT_PINS[i], 1); // Apagar (Ánodo Común)
    }
}