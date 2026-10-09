idf_component_register(SRCS "main.c" "controller_ui.c" INCLUDE_DIRS "." REQUIRES esp_wifi esp_event nvs_flash esp_netif json mbedtls esp-tls)
