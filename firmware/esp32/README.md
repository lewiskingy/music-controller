# ESP32 touchscreen firmware

Current firmware targets the original **Waveshare ESP32-S3-Touch-LCD-4.3**, 800×480 GT911, 16 MB flash / 8 MB PSRAM, rotated to a **480×800 portrait UI**. Other variants require separate support. See [hardware requirements](../../docs/hardware.md).

## Install or update

**Start with [Getting Started](../../docs/getting-started.md).** Windows x64 users should use the [packaged installer](../../installer/README.md): new setup installs firmware and private settings; update mode preserves settings. No ESP-IDF installation is required for that route.

The manual instructions below use **Actions artifact filenames**. Tagged installer releases use names such as `waveshare-4.3-application.bin`; these are separate bootloader/partitions/OTA/application files, not a merged image. Do not substitute one filename for another without checking its role and address. Use the release installer to manage those assets.

## Release binaries and flash addresses

**`waveshare-4.3-application.bin` is the application firmware**, equivalent to `music_controller.bin` in an Actions artifact. The Actions ZIP also includes other binaries, a merged image and debugging/build files, so its size is not the application's size.

For the current board/layout, a manual complete installation uses these public release files plus locally generated private settings:

| File | Address | Purpose |
| --- | --- | --- |
| `waveshare-4.3-bootloader.bin` | `0x0` | Startup code |
| `waveshare-4.3-partitions.bin` | `0x8000` | Flash layout |
| `controller-nvs.bin` (private, generated locally) | `0x9000` | Wi-Fi, gateway URL/key and initial preferences |
| `waveshare-4.3-otadata.bin` | `0xf000` | Application boot selection |
| `waveshare-4.3-application.bin` | `0x20000` | Controller application |

Use `release-manifest.json` as the authority for the chosen release's public component offsets and hashes. Private NVS is never a public release asset. An ordinary compatible application-only update preserves NVS; the installer also resets boot selection as needed. A complete erase requires provisioning settings again.

Where supplied, `music-controller-full-flash.bin` combines the public firmware components for writing at `0x0`; restore private NVS afterward. v0.5.0 supplies separate binaries only, not a merged release image. The `.tar.gz` release assets contain gateway source or the browser prototype, not ESP32 firmware. See the [complete release asset guide](../../docs/releases.md).

## Current features and limits

Now Playing, Players, Providers, artwork, Queue, Browse and Search use separate full-screen views. Firmware supports playback/seek, 5% volume stepping, persisted player/provider and colour preferences, provider icons and backlight timeouts. Sonos Pause is disabled while playing; Stop is available. See [current project limits](../../README.md#current-limits).

The Loading image appears during boot and briefly before backlight-off. Auto power mode uses battery timing even on USB; choose USB/external manually for that timeout. This is backlight power saving, not deep sleep. Wireless OTA is not implemented.

## Gateway prerequisite

Deploy the [companion gateway](../../gateway/README.md) and a trusted HTTPS/WSS frontend. Music Assistant credentials stay on that host; the device receives a separate matching controller key. Wi-Fi DNS must resolve the WSS hostname and its certificate must be trusted by the ESP-IDF bundle. Enter **your own** URL, for example `wss://controller.example.net/controller/device/ws`; the manual provisioner's historical default is author-specific. Never bypass TLS verification.

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

CI builds the same IDF version and uploads bootloader, partition table, application, flash arguments, checksums and `music-controller-full-flash.bin` as an Actions artifact. Tagged releases also provide installer-compatible per-profile assets; their filenames differ from Actions artifacts.

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

## Wi-Fi power saving

Wi-Fi modem sleep is **disabled by default** (`WIFI_PS_NONE`), on battery and
USB, including while the backlight is off. The screen timeout does not disconnect
Wi-Fi or suspend the gateway connection. Keeping the radio awake uses more power;
battery runtime and sleep/wake connectivity need physical testing.

To explicitly enable modem sleep, set `wifi_sleep` in the `controller` NVS
namespace to a `u8` value of `1`. This selects `WIFI_PS_MIN_MODEM`, which
should maintain association while periodically sleeping the radio. Missing,
zero, invalid or unreadable values select `WIFI_PS_NONE`. The setting is read at
boot; restart after changing it. Startup logs report the selected mode.

The manual `provision.py` tool now asks “Enable Wi-Fi modem sleep to save power?”
and defaults to No. For a manually maintained NVS CSV, the opt-in row is:

```csv
wifi_sleep,data,u8,1
```

The Windows installer offers an unchecked **Enable Wi-Fi sleep** checkbox during
new-device setup; CLI provisioning offers the same opt-in. Both write this key
as 0 or 1. Normal updates preserve the stored setting. Existing NVS images need no changes for the new default: install an
application-only firmware update to preserve credentials and preferences. Only
regenerate/flash a private NVS image if deliberately changing provisioning;
replacing NVS also replaces its other saved settings.

## Hardware acceptance and diagnostics

Verify live metadata, player selection across reset, transport/volume on the correct output, Providers → change → Save across reset, and recovery after Wi-Fi/gateway interruption. The outer screen never scrolls or stacks primary views.

Use `idf.py monitor`, a serial terminal or `py -m serial.tools.miniterm COM5 115200` (requires pyserial) to inspect logs. Look for LCD/LVGL initialisation, DHCP, WebSocket handshake, `gateway/ready`, MA responses and heartbeat/reset reasons. A blank panel before networking points to display/board bring-up; connection failures need DNS/TLS/token/upstream checks. Never post the private NVS image or credentials with diagnostics.


## Ordinary manual updates

For an already provisioned board with the same partition layout, write only the application from the chosen successful Actions artifact to preserve NVS:

```powershell
py -m esptool --chip esp32s3 --port COM5 --baud 460800 write-flash 0x20000 music_controller.bin
```

Use the installer update mode for tagged release assets. A full merged flash can replace NVS; restore your private NVS afterward. Changing partition layouts requires explicit migration/recovery instructions rather than assuming an application-only update is compatible.

## Development and rendering

Follow [firmware AGENTS.md](AGENTS.md) and [development checks](../../docs/development.md). Historical investigations and milestone notes are preserved in [firmware history](../../docs/firmware-history.md); they are not current installation instructions.

## Buffered presentation and startup loading page

The pinned BSP allocates one full logical drawing buffer and two RGB panel buffers
in PSRAM (three 768,000-byte buffers, about 2.20 MiB), with portrait rotation and
anti-tearing mode 3. `controller_display.c` replaces its rotated LVGL flush and RGB
completion callback after BSP initialization; hardware pins, touch rotation and
the LVGL task remain provided by BSP 1.0.7. No managed component is modified.

The adapter rotates dirty rectangles in 16×16 tiles. Both external-memory reads
and writes are contiguous within each tile, rather than scattering each pixel
across PSRAM cache lines. It submits only after LVGL's last dirty area, then waits
for safe retirement before copying the changed native rows into the old buffer.
The logical canvas is never submitted as a landscape panel buffer.

IDF 5.4.2's bounce-frame callback reports when a complete source frame has been
copied to DMA, not which framebuffer was selected. A callback already in flight
on the other core can arrive around submission. The adapter drains notifications
**after** submission and waits for two subsequent completion events before
reusing the old buffer. It accepts the additional latency (up to roughly two
panel periods in normal operation) to avoid writing pixels still being consumed.
Each wait has a one-second timeout that fails explicitly rather than silently
reusing an unsafe buffer. Sparse control updates still render only dirty regions.

The panel clock is requested at 12 MHz instead of the BSP's 14 MHz; nominal refresh
is about 29 Hz with the BSP porch timings (the divider may round the clock). Two
20-line internal DMA bounce buffers use about 62.5 KiB total, 31.25 KiB more than
the previous 10-line default. The data-cache line is 64 bytes, compiler optimization
is performance (-O2), and instructions execute and read-only data is read from PSRAM so NVS
writes do not disable the display's external-memory cache. PSRAM instruction/data
placement costs additional external RAM; the firmware retains the 64 KiB internal
allocation reserve. Stale configuration fails compilation: regenerate sdkconfig
from defaults, or apply all new settings before rebuilding.

This follows ESP-IDF's [RGB bandwidth/cache guidance](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer).
The physical symptom was a vertically shifted/repeated image during navigation,
which is consistent with scanout starvation. Buffer ownership and retirement are
also hardened; physical testing is needed to establish whether all observed
flicker is eliminated. No unconditional per-VSYNC restart workaround is enabled.

Rendering rules for future changes:

- Build screen/menu/dialog and substantial content changes in one LVGL transaction:
  on the LVGL task, or under `lvgl_port_lock` from application tasks. Finish the
  entire change before unlocking. Screen navigation invalidates the complete root.
- Keep button feedback, selection, typing, search results and playback progress on
  the same buffered presentation path. Dirty-region rendering avoids repainting
  unrelated artwork; it does not permit writes into the active panel buffer.
- Do not clear the visible panel, draw directly with panel APIs, or manually swap
  buffers from UI code. The adapter owns rotation, synchronization and retirement.
- Do not force intermediate refreshes during a transition. Keep `lv_refr_now` on
  the BSP LVGL task; the upstream BSP's flush relies on task-specific notifications.
- Skip unchanged text updates. Download assets outside UI locks and apply complete,
  validated assets inside one transaction.

Startup presents the bundled Loading splash before building the main UI and saved
appearance in one locked transaction. The loading frame stays visible until the
completed main frame is presented. It does not wait for Wi-Fi, gateway or artwork.
Later transitions retain the previous frame instead of inserting a loading page.

WebSocket Ping/Pong control frames bypass the JSON assembler and do not discard a
partial message. Individual fragment logs use DEBUG rather than INFO; assembled
message sizes and response summaries remain visible.
