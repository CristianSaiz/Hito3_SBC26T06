idf_component_register(
    SRCS 
        "main.c"
        "src/spiffs_manager.c"
        "src/wifi_manager.c"
        "src/web_server.c"
        "src/ble_prov.c"
        "src/adc_sampler.c"
        "src/display_7seg.c"
        "src/oled_display.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        nvs_flash
        esp_netif
        esp_event
        esp_wifi
        esp_http_server
        spiffs
        bt
        driver
        esp_adc
        ssd1306
)

# Empaquetado automático del directorio 'spiffs/' en la partición 'spiffs' de la Flash
spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)