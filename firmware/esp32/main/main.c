#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"

static const char *TAG = "music-controller";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Music Controller firmware foundation");
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip cores: %d, features: 0x%lx", chip.cores, (unsigned long)chip.features);
    ESP_LOGI(TAG, "Screen contract: 800x480, exactly one active view");
    ESP_LOGW(TAG, "Display, touch, Wi-Fi and Music Assistant are not yet initialized");
    // Deliberately hardware-neutral: display/touch GPIO and BSP depend on
    // the exact Waveshare board revision. No guessed pin mappings.
}
