include("${CMAKE_CURRENT_LIST_DIR}/../assets/splash.cmake")
controller_splash_asset(splash_source)
idf_component_register(SRCS "main.c" "controller_ui.c" "controller_display.c" "controller_splash.c" "${splash_source}" INCLUDE_DIRS "." REQUIRES esp_http_client esp_wifi esp_event nvs_flash esp_netif json mbedtls esp-tls)

# Reject unexpectedly large application frames; keep reports for diagnosis.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=4096)

# LVGL itself must see the allocator header, as must every consumer of lvgl.h.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

if(NOT CONFIG_LV_MEM_CUSTOM OR NOT CONFIG_LV_MEM_CUSTOM_INCLUDE STREQUAL "controller_lvgl_memory.h")
    message(FATAL_ERROR "Music Controller requires CONFIG_LV_MEM_CUSTOM=y and CONFIG_LV_MEM_CUSTOM_INCLUDE=controller_lvgl_memory.h; regenerate sdkconfig from defaults")
endif()
