idf_component_register(SRCS "main.c" "controller_ui.c" INCLUDE_DIRS "." REQUIRES esp_http_client esp_wifi esp_event nvs_flash esp_netif json mbedtls esp-tls)

# Reject unexpectedly large application frames; keep reports for diagnosis.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=4096)
