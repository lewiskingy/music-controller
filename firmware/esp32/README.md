# ESP32-S3 firmware foundation

**Status:** LCD/touch bring-up is proven on hardware. This branch adds a first **read-only connected Now Playing** firmware: Wi-Fi, TLS WebSocket to the Atlas device gateway, player/queue state and LVGL text. It does not yet implement artwork, queue browsing, transport buttons or OTA.

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

## LCD + touch bring-up milestone

The next build uses the pinned third-party Waveshare 4.3-inch board support
component `bobscott45/waveshare_esp32_s3_touch_lcd_4_3@1.0.7` and LVGL
`8.3.11`. It initialises the RGB panel, GT911 touch and backlight, and
shows a static **Now Playing** placeholder and a touchable button. This is
a hardware bring-up image, not a connected media controller.

**Before flashing:** confirm the board is the **4.3-inch 800×480 GT911**
variant (not the separate 4-inch 480×480 product). The previously flashed
USB serial skeleton remains the recovery baseline. Leave the battery
disconnected and power from USB.

CI must pass before downloading its merged flash image. After flashing,
check for `LCD and LVGL initialised` in the serial log. Expected: a dark
Now Playing screen and a button. Touch should show LVGL's pressed visual
feedback; no music control commands are sent yet. Report any reset loop,
white screen, touch offset or display flicker with the serial log.

The component is pinned for repeatable builds. Board pin assignments are
supplied by the BSP rather than copied into our application.

## Connected Now Playing — private configuration

**Do not use the Music Assistant `media` password on the device.**
Atlas holds MA account credentials; the ESP32 receives a separate device token.
Neither Wi-Fi credentials nor device token are built into public firmware or
GitHub Actions artifacts.

1. Deploy the companion Atlas native-device gateway PR. On Atlas generate a
   random token using `python3 -c "import secrets; print(secrets.token_urlsafe(32))"`
   and append `CONTROLLER_DEVICE_TOKEN=<token>` to the existing private
   `/opt/atlas-secrets/music-controller.env`. Retain the `docker-secrets`
   ownership/permissions and run normal Atlas deployment.
2. On the Windows flashing computer, obtain ESP-IDF v5.4.2's NVS generator.
   You can clone its source without installing the whole build toolchain:
   `git clone --depth 1 --branch v5.4.2 https://github.com/espressif/esp-idf.git esp-idf-5.4.2`.
   In PowerShell set `$env:IDF_PATH=(Resolve-Path .\\esp-idf-5.4.2).Path`.
   Run `py firmware/esp32/provision.py` from the music-controller checkout.
   Enter the Wi-Fi SSID/password, Atlas device token, and optionally the
   Music Assistant player ID. The default URL is
   `wss://music.theflat.me.uk/controller/device/ws`.
   The generator may require the ESP-IDF Python NVS dependencies.
3. The generator writes **private** `firmware/esp32/controller-nvs.bin`
   (24 KiB). Never upload it or commit it.
4. Flash the **full merged firmware image** at address `0x0` as before.
   Then flash the private NVS partition at address `0x9000`:

   ```powershell
   py -m esptool --chip esp32s3 --port COM4 write-flash 0x0 music-controller-full-flash.bin
   py -m esptool --chip esp32s3 --port COM4 write-flash 0x9000 firmware/esp32/controller-nvs.bin
   py -m serial.tools.miniterm COM4 115200
   ```

   Adjust paths and COM port to your actual directories. **Never** flash
   `music_controller.bin` at `0x0`; it is only the application partition.
5. With LAN split DNS configured, the device should show Wi-Fi connection,
   gateway authentication, selected player and current track/artist. A
   disconnected device retries Wi-Fi and WebSocket automatically.

The first build defaults to the first available Music Assistant player. For a
specific Sonos or Cast output, provide its exact MA `player_id` during NVS
provisioning. The device validates Atlas's HTTPS certificate; do not disable
certificate verification as a workaround for local DNS problems.

**Important:** A full merged image can overwrite the NVS region. Provision
after each full-flash operation. Future OTA application-only updates should
preserve the NVS partition. The device token is shared by this initial device
endpoint; rotate and reprovision if it is disclosed.

### Acceptance before adding touch controls

- Boots without resets on the actual 4.3-inch 800×480 hardware.
- Wi-Fi and certificate-verified WSS connection work.
- Gateway rejects missing/wrong device tokens.
- Real selected player and Now Playing metadata appear.
- Changing playback from Music Assistant/Symfonium updates the display.
- Restarting Atlas or Wi-Fi recovers without reflashing.
- No MA account password or Wi-Fi credentials are committed.

This is a first vertical slice, not yet feature parity with the HTML controller.
