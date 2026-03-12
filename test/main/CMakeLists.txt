idf_component_register(
    SRCS "test_main.c" "main_under_test.c"
    INCLUDE_DIRS "."
    REQUIRES unity esp_netif esp_timer esp_driver_ledc esp_driver_gpio spi_flash esp_wifi nvs_flash lwip wifi_provisioning bt
)
