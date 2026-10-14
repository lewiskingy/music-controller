# ESP32-S3 touchscreen firmware

ESP-IDF v5.4.2/LVGL firmware for the Waveshare **ESP32-S3-Touch-LCD-4.3**, 800×480 GT911 touch variant with 16 MB flash. Check your exact PCB/board model; the separate 4-inch 480×480 device is not interchangeable.

## Current features

- LCD/touch, Wi-Fi and certificate-verified WSS through the companion gateway.
- Live Now Playing/player and queue metadata, player selection persisted to NVS.
- Play/Pause, Stop, Next, Previous and volume stepping at 5% boundaries.
- Separate full-screen Providers selection, explicit Save and persistence across reset. It does not yet filter browsing/search.
- Exclusive LVGL screens and bounded WebSocket fragment reassembly (128 KiB).

Sonos Pause is disabled while playing because of observed queue hopping; use Stop. Native artwork, Queue/Browse/Search screens, battery/sleep management and signed OTA delivery remain future work. Two 5 MB OTA slots are reserved; OTA is not implemented yet. Source/CI validation does not replace testing your particular hardware and Music Assistant version.

## Gateway prerequisite

Deploy [gateway/](../../gateway/README.md) on your own host, with a dedicated Music Assistant account, random `CONTROLLER_DEVICE_TOKEN` and trusted HTTPS frontend. The device holds the separate device token, never the Music Assistant account password. Atlas is an existing deployment option, not required.

Your Wi-Fi DNS must resolve the WSS hostname to the frontend. Its certificate must be trusted by the ESP-IDF CA bundle. Do not bypass verification. The provisioner retains the existing Atlas URL as a convenience default; **enter your own URL** such as `wss://controller.example.net/controller/device/ws` for other installations.

## Build from source

Activate an installed ESP-IDF **v5.4.2** environment, then from the repository root:

```sh
. "$HOME/esp/esp-idf/export.sh"
cd firmware/esp32
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Substitute your serial port. The pinned BSP `bobscott45/waveshare_esp32_s3_touch_lcd_4_3@1.0.7` supplies display/touch pin assignments; LVGL is pinned to 8.3.11. Dependencies are in `main/idf_component.yml`. Exit monitor with Ctrl+]. Start with USB power and leave the battery disconnected until hardware/power suitability is confirmed.

CI builds the same IDF version and uploads bootloader, partition table, application, flash arguments, checksums and `music-controller-full-flash.bin` as an Actions artifact. This is separate from tagged browser-prototype releases.

## Flash a prebuilt artifact (Windows)

Download the successful **ESP32 firmware** Actions run's `music-controller-esp32s3-...` ZIP and extract it. Connect the board's USB port with a data-capable cable and identify the COM port in Device Manager.

```powershell
py -m pip install --upgrade esptool
py -m esptool --chip esp32s3 --port COM5 flash-id
py -m esptool --chip esp32s3 --port COM5 --baud 460800 write-flash 0x0 music-controller-full-flash.bin
```

Confirm the correct board and **16 MB** flash before writing. If connection fails, hold BOOT while pressing RESET and retry. Flashing replaces the vendor demonstration firmware; back it up first if needed. Never write the application-only `music_controller.bin` at `0x0`; use the merged image there. The application partition starts at `0x20000` in this repository's partition table.

## Provision private NVS

Run locally from your checkout, with ESP-IDF v5.4.2 activated (`IDF_PATH` set):

```sh
python firmware/esp32/provision.py
```

On a Windows machine without the full toolchain, clone the v5.4.2 IDF source and install the NVS generator dependencies:

```powershell
git clone --depth 1 --branch v5.4.2 https://github.com/espressif/esp-idf.git esp-idf-5.4.2
$env:IDF_PATH=(Resolve-Path .\esp-idf-5.4.2).Path
py -m pip install cryptography
py firmware/esp32/provision.py
```

Enter Wi-Fi SSID/password, **your certificate-verified WSS gateway URL**, matching device token and optionally the exact MA player ID. Without a player ID, firmware selects an available player. The tool writes private `firmware/esp32/controller-nvs.bin` (24 KiB) containing Wi-Fi/device credentials. It is ignored by Git and must never be uploaded or attached to an issue.

After flashing full firmware, flash NVS at `0x9000`:

```powershell
py -m esptool --chip esp32s3 --port COM5 write-flash 0x9000 firmware/esp32/controller-nvs.bin
```

Adjust paths if the extracted artifact and checkout are in different directories. **Full merged images may overwrite the NVS region: always flash private NVS afterwards.** Resetting the original NVS also restores its originally provisioned preferences; on-device player/provider choices made since then can be lost. Application-only updates can preserve NVS if written at the correct application offset.

## Hardware acceptance and diagnostics

Verify live metadata, player selection across reset, transport/volume on the correct output, Providers → change → Save across reset, and recovery after Wi-Fi/gateway interruption. The outer screen never scrolls or stacks primary views.

Use `idf.py monitor`, a serial terminal or `py -m serial.tools.miniterm COM5 115200` (requires pyserial) to inspect logs. Look for LCD/LVGL initialisation, DHCP, WebSocket handshake, `gateway/ready`, MA responses and heartbeat/reset reasons. A blank panel before networking points to display/board bring-up; connection failures need DNS/TLS/token/upstream checks. Never post the private NVS image or credentials with diagnostics.

## Tests and CI

From the repository root, `python -m pytest tests --ignore=tests/prototype -q` runs source-contract checks. The firmware workflow runs these before the real IDF build. Gateway-only changes do not trigger it; shared `contracts/` changes do. See [development](../../docs/development.md).
