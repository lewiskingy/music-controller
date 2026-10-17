"""Run the real presenter with simulated RGB interrupts, not just source assertions."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_buffered_display_pixels_and_retirement(tmp_path):
    stubs = ROOT / "tests/native-display"
    # Forward hardware includes to one small host shim. Production source is
    # compiled unchanged so the same flush and retirement code is exercised.
    headers = ["sdkconfig.h", "esp_err.h", "esp_attr.h", "esp_lcd_types.h",
               "esp_lcd_panel_ops.h", "esp_lcd_panel_rgb.h", "esp_log.h",
               "esp_timer.h", "lvgl.h", "freertos/FreeRTOS.h",
               "freertos/semphr.h", "bsp/lvgl_port.h"]
    for name in headers:
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "display_test_stubs.h"\n')
    binary = tmp_path / "display-acceptance"
    subprocess.run([
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-I", str(tmp_path), "-I", str(stubs),
        "-I", str(ROOT / "firmware/esp32/main"),
        str(ROOT / "firmware/esp32/main/controller_display.c"),
        str(stubs / "acceptance.c"), "-o", str(binary),
    ], check=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    assert "203 complete/incremental frames" in result.stdout


def test_firmware_installs_adapter_before_boot_splash():
    """The independently tested presenter must also be wired into the firmware."""
    main = (ROOT / "firmware/esp32/main/main.c").read_text()
    startup = main[main.index("void app_main(void)"):]
    assert startup.index("lvgl_port_init(panel, touch)") < startup.index("controller_display_init(panel)")
    assert startup.index("controller_display_init(panel)") < startup.index("controller_splash_show(true)")
    cmake = (ROOT / "firmware/esp32/main/CMakeLists.txt").read_text()
    assert '"controller_display.c"' in cmake
    assert '"controller_splash.c"' in cmake
    defaults = (ROOT / "firmware/esp32/sdkconfig.defaults").read_text().splitlines()
    for setting in ["CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT=20",
                    "CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y",
                    "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y", "CONFIG_SPIRAM_RODATA=y",
                    "CONFIG_COMPILER_OPTIMIZATION_PERF=y", "CONFIG_LV_USE_PNG=y"]:
        assert setting in defaults
