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

## Windows laptop: flash a prebuilt GitHub Actions artifact

After CI succeeds, open the firmware workflow run in GitHub Actions and
download its `music-controller-esp32s3-...` artifact ZIP. Extract it locally.
Use the **USB-C port labelled USB** on the Waveshare board and a data-capable
cable. Leave the battery disconnected for the initial test.

Install Python 3 and the official Espressif flashing utility on Windows:

```powershell
py -m pip install --upgrade esptool
```

Identify the board's COM port in Windows Device Manager. From PowerShell in
the extracted artifact directory, substitute the correct port:

```powershell
py -m esptool --chip esp32s3 --port COM5 flash_id
py -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 music-controller-full-flash.bin
```

The first command confirms a responding ESP32-S3 and detects flash capacity.
**Only flash if the reported flash capacity is 16 MB** and the model matches
the expected Waveshare ESP32-S3-LCD-4.3. If Windows cannot connect, hold
BOOT while pressing RESET to enter download mode, then retry. Never use
`erase_flash` for this first test.

For serial logging, install an appropriate serial terminal or the ESP-IDF
monitor. The separate USB-C port marked UART provides a USB-to-serial bridge;
the USB port may expose the ESP32-S3's native USB serial/JTAG interface
depending on the firmware configuration. The screen staying blank with this
skeleton is **expected**.

**Caution:** Flashing overwrites existing vendor demonstration firmware.
If preserving the demo matters, back up flash first with esptool or obtain
the official Waveshare recovery image.
