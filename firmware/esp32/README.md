# ESP32-S3 firmware foundation

**Status:** hardware-neutral boot skeleton. It logs startup over USB serial; it does **not** yet initialise LCD, touch, Wi-Fi, Music Assistant or OTA.

## Build

Use ESP-IDF v5.4.2 (also pinned in CI):

```bash
. "$HOME/esp/esp-idf/export.sh"
cd firmware/esp32
idf.py set-target esp32s3
idf.py build
```

The custom partition table reserves two 5 MB OTA slots for future rollback. Confirm board flash size and exact model before flashing.

## First device installation

After checking Waveshare SKU/PCB revision, flash size and data-capable USB port:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Replace the port with the detected one. Expected serial log: `Music Controller firmware foundation`, ESP-IDF version and a notice that display/touch are not yet initialised. Exit monitor with Ctrl+].

CI uploads bootloader, partition table, application binary, flash arguments and checksums as **workflow artifacts**, not GitHub Releases. The web prototype's `latest` release remains separate.

Next: confirm hardware, integrate exact Waveshare display/touch BSP and LVGL, show one focused 800×480 screen, then Wi-Fi/gateway and OTA. Never commit credentials or private network configuration.
