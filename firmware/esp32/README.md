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

## Diagnostic firmware (blank display / stalled gateway)

The diagnostic build logs each stage to serial without printing SSID passwords,
device tokens or other credentials. A visible dark-blue screen with white
Music Controller heading, player, track and green status is created before
Wi-Fi setup. State changes are rendered by the main task, rather than calling
LVGL directly from Wi-Fi/WebSocket callbacks.

Look for `Diagnostic screen created`, `Wi-Fi starting association`,
`DHCP address`, `Starting WSS connection`, `WebSocket handshake completed`,
`Gateway authenticated and ready`, and repeating `Heartbeat` lines.
If the serial log reports the screen created but the physical panel is still
blank, treat it as an LCD/LVGL rendering issue independent of networking.

**Flashing:** Download the successful ESP32 firmware Actions artifact for
this commit. Flash the **merged** `music-controller-full-flash.bin` at `0x0`.
Because a merged image includes the NVS flash region, **reflash the existing
private `controller-nvs.bin` at `0x9000` immediately afterwards**. No need
to regenerate the NVS image or change its credentials. Then restart and
capture serial output for at least 30 seconds. Never publish the NVS image.

## WebSocket response reassembly fix

The first connected hardware test established Wi-Fi, TLS, WebSocket and
`gateway/ready`, but large `players/all` and `player_queues/all` replies
were silently dropped. This revision assembles fragmented WebSocket text
messages in bounded PSRAM (128 KiB maximum), logs fragment lengths and
completed response counts, reports API errors and refreshes periodically.
Messages larger than the bound are rejected rather than risking memory use.

After CI succeeds, flash the new **merged** firmware image at `0x0`, then
reflash your **existing private** `controller-nvs.bin` at `0x9000`.
No configuration changes are required. Observe `MA request p`, `WS frame`,
`WS message assembled` and `MA response p/q` in the serial monitor.

## Native touchscreen milestone — Now Playing and Players

The ESP32 now has two **mutually exclusive, full-viewport LVGL screens**:
Now Playing and Player Selection. The 800×480 display never stacks views,
and only the player list scrolls. Queue, Browse and Search remain future
separate screens; they are **not** mixed into Now Playing.

- **Now Playing:** player name is a large touch target opening Player
  Selection; track/artist, Play/Pause, Previous, Next, Stop, and 10-point
  volume decrement/increment are displayed.
- **Player Selection:** a dedicated list of Music Assistant players, with a
  Back button to Now Playing. Selecting a player returns immediately to Now
  Playing and requests its live state.
- The selected player ID is persisted under `controller/player_id` in NVS,
  so subsequent restarts retain the selection. The initial ID provisioned
  via `provision.py` remains the default on first boot.
- Touch events enqueue actions for the firmware main task; the LVGL task
  does not perform network operations. Responses from Music Assistant
  remain authoritative for playback state and volume.
- **Sonos Pause is disabled while playing** because of the previously
  observed queue-hopping behaviour (Atlas #587). Stop remains available.
- Player control commands use the existing allowlisted Atlas gateway; no
  new Music Assistant credentials or gateway deployment are required.

### Flash and acceptance

Download the successful ESP32 Actions artifact for this PR. Flash
`music-controller-full-flash.bin` at `0x0`, then reflash your **existing**
private `controller-nvs.bin` at `0x9000`. The merged image overwrites NVS,
so preserve this order. No regeneration of your device token or Wi-Fi
credentials is needed.

Verify on the Waveshare: Now Playing shows the previously selected player;
tapping its name replaces the entire screen with the player list; Back
returns to Now Playing; selecting another player updates metadata and
survives RESET; Stop, Next, Previous, Play and volume reach the correct
player. Confirm Sonos Pause remains unavailable while playing, and no
network or player list appears behind the active screen.

The source has not yet been hardware-tested for this milestone; review CI
and serial logs before considering it validated.

## Music Provider selection and 5% volume stepping

A dedicated full-screen **Music Providers** view is accessible from Now
Playing. It uses Music Assistant's provider listing through the Atlas gateway
(companion Atlas PR required), excludes non-music integrations, and presents
checkboxes for available music sources. All are selected by default until the
first user change. The selected provider IDs are stored independently of
the output player ID in NVS under `controller/music_sources`. An intentionally
empty selection is preserved.

**Scope:** This milestone implements provider **discovery and selection**.
Browse and Search screens and their provider-filtered API calls are roadmap
follow-ups; the selection screen explicitly says so rather than claiming
filters are already applied. Validate the installed Music Assistant version's
provider listing response and `type=music`/instance ID fields during hardware
testing. An API error must not be treated as an empty list.

Volume touch controls use **5% boundaries**. The first upward press at 12%
goes to 15%; the first downward press goes to 10%. From exactly 15%, the next
press goes to 20% or 10% respectively. Values clamp at 0–100%. The UI reads
back Music Assistant's reported volume; it does not assume a command succeeded.

**Deploy order:** merge and deploy the companion Atlas gateway PR before
flashing the firmware. Flash the full merged firmware image, then the
existing private NVS image at `0x9000`. The first provider change is saved
on-device and survives subsequent resets; re-flashing the full merged image
and NVS resets the choice to the provisioned image's state.

## Provider checkbox reset investigation

On hardware, the provider list loads but tapping a checkbox reportedly blanks
the screen, returns to Now Playing, and fails to retain the choice. The
exact reset reason has not yet been captured.

This patch **separates checkbox changes from flash writes**. Tapping a
checkbox only changes in-memory selection; pressing **Save** explicitly
persists the selected provider IDs to NVS. A confirmation appears on the
Providers screen. The firmware logs checkbox transitions and the ESP32 reset
reason at startup, so a remaining crash can be diagnosed rather than guessed.
The provider screen remains exclusive and full-screen.

Hardware acceptance: open Providers, uncheck one, confirm the screen remains
visible, press Save, confirm the success label, return via Back, reopen and
verify the selection; then press RESET and verify it persists. If the device
reboots, capture the serial log including `Boot reset reason`, any panic
backtrace and the final checkbox/save event. Avoid posting device tokens.
