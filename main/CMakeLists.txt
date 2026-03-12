idf_component_register(SRCS "main.c" "wwvb_codec.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_netif esp_timer esp_driver_ledc esp_driver_gpio spi_flash esp_wifi nvs_flash lwip wifi_provisioning)