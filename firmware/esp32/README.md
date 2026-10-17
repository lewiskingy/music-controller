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

## Rich portrait UI — first component increment

The `feature/rich-portrait-ui` branch implements the first native LVGL design
increment from `docs/design/music-controller-design.html`. It replaces the
landscape screen compositions with one 480×800 shell: header (64 px), content
(468 px), shared playback dock (200 px at y=532), navigation (68 px at y=732).
Only content panes switch; the same dock objects remain mounted for Now
Playing, Players and Providers. Lists scroll inside content.

`controller_ui.c/.h` owns shared colours, sizing, labels, buttons, selection
row styles, artwork placeholder and dock rendering. It has no network or NVS
dependencies; the application passes a selected-player snapshot and receives
action callbacks. `playback_policy.h` owns the host-testable Play/Stop and
5% volume command policy.

The centre control sends Stop while playing, Play otherwise, including paused
players. There is no Pause or fourth Stop button. Commands capture the selected
player and visible state at tap time, are queued off the LVGL task, and reject
a changed/unavailable target rather than controlling a newly selected room.
One pending command disables the dock until its correlated acknowledgement,
send failure, disconnect or a 10-second timeout. Playback and volume remain
authoritative Music Assistant state.

The BSP's `CONFIG_LVGL_PORT_ROTATION_90=y` rotates rendering and GT911 touch
coordinates together; no second application-level rotation is applied. The
BSP 1.0.7 source exposes this option and uses three RGB framebuffers for
rotation; allow roughly 2.2 MiB for those buffers before other allocations.
Physical orientation, touch alignment and available PSRAM still require
validation on the actual board. Use a clean SDK configuration or set the same
rotation choice in menuconfig when reusing an existing build directory.

This historical first increment retained +/- volume controls. The revised
docks below supersede that geometry and add a vertical volume slider. Navigation exposes only the
implemented Playing, Sources and Players views. Queue/Browse/Search and real
artwork follow later; no placeholder navigation claims those features work.
Provider selection is exclusive; explicit Save persists the single choice.

### Validation and device acceptance

Run `python -m pytest tests/test_playback_policy.py tests/test_esp32_focus_controls.py
tests/test_esp32_providers_volume.py tests/test_esp32_provider_save.py` for the
command policy and integration contracts. CI builds against ESP-IDF 5.4.2 and
LVGL 8.3.11. A passing build is not hardware acceptance.

After CI succeeds, flash the workflow's merged image at 0x0, then restore
your existing private NVS image at 0x9000 as described above. Verify:

- Portrait layout and touch alignment at all four corners; no shell scrolling.
- Real track/player metadata and identical dock position on all three views.
- Previous, Play/Stop, Next and volume on each view; repeated taps do not
  submit duplicate commands while pending.
- Sonos Play after Stop (resume/restart semantics are provider-dependent).
- Player changes, unavailable players, reconnects and rejected commands.
- Provider radio choice/Save operation and persistence after RESET.
- Stable memory, no resets, and responsive touch while gateway responses arrive.

Skip/volume capability metadata is not yet modelled by the existing adapter;
unsupported commands report gateway errors. Artwork decoding, full browsing
and keyboard layout remain later increments.

## Revised docks and seek timeline — design v1.1

The current feature branch implements the revised shell from the design
reference: 400×532 content at (0,64), persistent 80×532 VolumeDock at
(400,64), full-width 480×136 TransportDock at (0,596), and navigation at
y=732. Now Playing, Players and Providers share the same two dock instances.
Both selection lists now fit the narrower pane.

VolumeDock has a vertical 0–100% slider with a 48 px-wide touch target and
56×56 +/- buttons retaining the existing 5% boundary stepping. Slider drag
values are previews (marked ~), sent no more often than every 300 ms; the
final release value is retained even while an earlier command is pending.
Pending volume updates are coalesced to the latest value, never a backlog
of obsolete intermediate positions. A * marks unconfirmed volume. Reported
Music Assistant volume remains authoritative. Volume controls require the
player's explicit `volume_set` capability.

Now Playing adds album metadata and a seek timeline with elapsed/duration
labels. Queue position is interpolated locally once a second while playing,
using elapsed_time, its timestamp and playback_speed from queue snapshots.
The UI freezes the gesture's player/queue/item identity, previews time locally
and submits one `player_queues/seek` on release. Lost presses cancel; stale
track/player gestures are rejected. Positions clamp within the current track,
ending one second before duration to avoid advancing the queue.

Seeking requires a finite duration up to seven days, active queue, stable
queue item identity, non-live media, no explicit stream `allow_seek=false`,
and gateway support. Live radio/audio sources show Live; unknown duration
shows --:-- with a disabled timeline. Transport, volume and seek have separate
acknowledgement/timeout slots, so seeking does not disable volume or browsing.
A pending transport command disables seeking to avoid a skip/seek conflict.

**Companion Atlas gateway deployment required for seek:** the gateway now
allows integer-second `player_queues/seek` and advertises support through
`gateway/capabilities` with `data.seek=true`. On an older gateway, transport
and volume continue working and seeking remains disabled. No credentials or
NVS changes are required. The installed MA version still needs device
validation for active queue/stream fields and Sonos seek behaviour.

Hardware acceptance: check both slider directions and enlarged hit targets,
5% stepping at non-multiples (12→15 / 12→10), dragging volume during playback,
seek preview/release on a finite track, track or room changes during dragging,
radio/unknown-duration disabling, gateway failures/timeouts and reconnect.
Real album artwork and remaining Queue/Browse/Search screens are still deferred.

## Native LVGL acceptance screenshots

The firmware CI `ui-acceptance` job builds the actual `controller_ui.c`
against LVGL 8.3.11 on Linux. A headless display driver captures 480×800 RGB
framebuffers; no HTML rendering, SDL, physical board or Music Assistant is
required. A simulated LVGL pointer drives transport/navigation buttons and
slider gestures. Deterministic fixtures cover all three implemented views,
seek preview/pending, stopped, radio, long metadata, disconnected and
unavailable states.

Assertions check dock coordinates, transport order, minimum volume button
size, non-scrolling shell, geometry containment outside intentionally
scrolling lists, emitted intents, seek-on-release, stale gesture cancellation
and disabled controls. Metadata labels have bounded heights to avoid
expansion into adjacent controls. These are UI/component acceptance tests,
not a complete gateway or hardware integration simulation.

Each run uploads `music-controller-ui-screenshots-<commit>` with PNGs and
`acceptance.log`, including screenshots already captured if a later check
fails. No visual golden baseline is imposed yet. Physical rotation, GT911
alignment, PSRAM and actual player behaviour remain hardware checks.

Local execution with an LVGL v8.3.11 checkout:

```bash
cmake -S tests/native-ui -B build/native-ui -DLVGL_SOURCE_DIR=/absolute/path/to/lvgl
cmake --build build/native-ui -j2
mkdir -p artifacts/native-ui
python -m pip install -r gateway/requirements.txt
python tests/native-ui/mock_artwork.py build/mock-artwork
build/native-ui/ui_acceptance artifacts/native-ui build/mock-artwork
python tests/native-ui/convert_screenshots.py artifacts/native-ui
```

### Appearance and staged navigation

The header cog opens appearance settings. Six palettes (Green, Blue, Red, Orange, Purple, Grey) each support Dark and Light modes. Shared semantic LVGL styles apply to existing controls and newly created player/source rows. Selection is stored as `ui_palette` and `ui_light` in the `controller` NVS namespace and restored on boot. Invalid palette values fall back to Green.

The bottom tabs match the design: Playing, Queue, Browse, Search. All four tabs are functional. Use the header player selector for Players and Settings → Music provider for provider selection.

Native acceptance checks all twelve theme selections and their persistence callbacks once, with a single settings/edit example. Screenshots focus on functional states; theme permutations are no longer captured on every screen. Checks retain fixed-caption fit, navigation, disabled interactions and persistent dock geometry.

### Gateway-served album artwork

The companion Atlas gateway advertises `artwork_rgb565` and annotates current queue items with `controller_art_id`. Firmware fetches `/device/artwork/{id}` from the same provisioned gateway using the existing device token in a dedicated worker. A complete validated MCAR packet contains a 288 × 288 little-endian RGB565 cover. PSRAM holds the transfer and reusable album-art display buffer. Downloads never hold LVGL locks. Results are discarded after a player, queue, track or artwork change. Same-album artwork is reused. Artwork retains its original colours under every theme.

The gateway supplies a generic record cover for absent or failed provider art. An unreachable/older gateway leaves the local placeholder visible, without affecting playback controls. Companion changes are in Atlas PR #614.

Native acceptance mocks gateway packets using `tests/native-ui/mock_artwork.py`, then passes that fixture directory as a second argument to `ui_acceptance`. It checks artwork, malformed/truncated packets, stale responses and artwork changes; functional screenshots include real-cover fixtures and generic covers.

### Queue

Queue is enabled in the bottom navigation. It opens the page containing the selected player's current queue index, loads at most 20 tracks, highlights the current item, and shows artist and duration. Previous/Next page controls bound memory; Refresh retries the current page. Loading, empty, API error, timeout, disconnected, unavailable-track and older-gateway states are explicit. Search is enabled.

Tap-to-play sends `player_queues/play_index` with the stable queue item ID as `index`, not a numeric position. The companion gateway advertises `queue_item_play` and validates either form. Removed items fail through the API rather than playing a replacement at the same position. Responses match request ID, player, queue and offset; row taps capture player/queue/item identity. Playback remains authoritative and transport pending/ACK/timeout handling is reused.

Acceptance includes mocked queue loading, 20-item pages, last page, row playback, disabled/unavailable rows, stale player targets, errors, disconnection and functional Queue screenshots. Gateway contract tests cover stable IDs and bounded pagination.

### Browse

Browse starts with the selected music sources, then Albums, Artists, Playlists and Provider folders. Artists open albums; albums and playlists open tracks. Track taps and Play all send `player_queues/play_media` with the stable media URI and `option: replace` to the captured selected queue. This replaces that queue; selecting a source never switches the output player.

A shared two-line media row and the persistent transport/volume docks serve Queue and Browse. Pages hold at most 20 items, navigation is bounded to eight contexts, and Back, Refresh and page controls stay inside the content pane. Request IDs and navigation generations reject stale responses. Loading, empty, disconnected, unsupported-gateway, unavailable-item, API-error and ten-second timeout states are explicit. Playback taps also validate the selected player and queue.

The gateway advertises `browse` and translates `controller/browse` into fixed Music Assistant read commands, returning compact metadata. Library categories filter the selected provider; Provider folders exposes its native browse hierarchy where supported. This requires the companion Atlas feature branch. Search uses the same compact rows and detail navigation.

Native acceptance captures functional Browse sources, categories, albums, artists, playlists, tracks, folders and failure states, without repeating each screen for every palette. Gateway acceptance includes a real device WebSocket against a mocked Music Assistant upstream, partial responses, provider identity and bounded pagination. Physical touch and live-provider validation remain pending.

### Search and exclusive provider selection

Search is functional in the fourth navigation tab. Choose a provider in Settings → Music provider; exactly one row is selected, and tapping it again cannot clear the choice. Save persists the single provider using the existing `music_sources` NVS key. Legacy multi-selection is normalized to the first available matching provider; a missing or absent saved provider selects the first discovered source. Selecting a provider clears pending Browse/Search pages, leaves the output player unchanged and requires Save to persist across reboot.

Search accepts up to 48 Unicode characters and offers Tracks, Albums, Artists and Playlists. Tap Search or the keyboard Search key to submit; typing and changing the type invalidate results immediately without sending per-keystroke requests. Search and Browse use the full 480px content width and hide volume. The 480px-wide keyboard replaces the results/filter area while preserving the bottom transport dock. Dismissing it reveals results. Track taps replace the selected queue; other results open the existing Browse details, with Back returning to the retained Search results.

The authenticated gateway `controller/search` adapter searches only the chosen provider/type, asks for 21 results and retains at most 20 compact rows. A sentinel triggers “First 20 results - refine your search”; there is no unbounded search pagination. Request ID, generation and provider checks reject obsolete responses. Playback captures player, queue and stable media URI. Empty input/results, unavailable items, API errors, disconnect, unsupported gateway and ten-second timeout states are explicit. Retry with Search.

Current Music Assistant `music/search` accepts `providers: [id]`. For older versions that explicitly reject that argument, the gateway retries once with the selected provider's filtered `music/{type}/library_items` search. This remains scoped to that provider's indexed library and the UI labels “Library results”; it never falls back to global multi-provider search. Provider-native support and hardware typing comfort still require live validation.

Acceptance now produces 46 functional screenshots, including Search input, keyboard, four result types, loading, empty, error, timeout, disconnected and unsupported states. The native suite also verifies stable playback targets, stale results, immediate edit invalidation, keyboard submission, exclusive provider clicks and theme callbacks. Theme choices are exercised once without a screenshot matrix. Gateway tests include actual WebSocket search/playback against a mocked Music Assistant, bounded partial responses and the older-server fallback.

### View-dependent volume dock

Browse and Search hide the right VolumeDock and expand their content panes to 480 × 532. Lists and content controls use 432px width with 24px side margins. Search's keyboard spans the entire display width (x=0, width=480), including the space previously used for volume. The bottom transport dock and navigation retain their positions. Playing, Queue, Settings, Players and Providers restore the existing volume dock. Acceptance checks dock visibility and restoration, full-width content and keyboard bounds, wide list rows, and unchanged transport geometry. The existing 45 functional screenshot set is retained.

### Simplified Search keyboard

The full-width default map contains lowercase letters only, plus Symbols, Space, Backspace and Search. Symbols switches to digits 0–9 and £ $ # & apostrophe, hyphen, period, comma, slash, ! ? parentheses, + @ and colon; Letters returns to the alphabet without losing text. No case toggles or cursor-arrow keys are shown. Reopening the keyboard starts with letters. The gateway applies Unicode casefold to submitted queries and to the older-server library fallback, retaining punctuation. The stock font is supplemented with an original pound-sign glyph so £ renders in both the keyboard and input. Acceptance covers symbol entry, UTF-8 backspace, mode switching, Search submission and case-normalized gateway requests; screenshots total 46.

### Offline boot and sleep image

The original Halloween PNG is bundled in `assets/halloween.png` and compiled into
firmware, so it works immediately without Wi-Fi, gateway access or provisioning.
A full-screen top-layer splash with “Loading...” is presented before enabling
the backlight. It covers initial interface creation and is removed in the same
LVGL transaction that completes the initial UI. It does not wait for a network
connection and never stays behind the normal colour theme.

On the configured inactivity timeout, the same image covers the interface for
at least two seconds, measured after its frame has been presented. Then the
backlight turns off. Touch during those two seconds cancels sleep and consumes
the entire gesture; waking from backlight-off also consumes the first gesture.
Saving power preferences cancels an in-progress sleep splash and resets timing.
There is no blocking two-second delay; networking and UI timers continue.

The optional background feature has been removed: no Settings toggle, image
polling worker, upload page or gateway image routes. Legacy `ui_backdrop` NVS
values and gateway image-state files are ignored; no NVS reset is needed. Plain
palette and Light/Dark choices remain. Existing gateways work with this firmware;
a gateway release is only needed to remove the obsolete upload endpoints.

PNG decoding uses the existing PSRAM allocator; decoded pixels and decoder
scratch allocations exist only while the splash is displayed and are released
when it is removed. The embedded PNG adds about 405 KiB to the application;
there is no recurring image download or persistent background pixel buffer.
Local builds require `CONFIG_LV_USE_PNG=y` (or regenerate defaults). Native
acceptance renders boot/sleep and post-splash screens; physical acceptance must
check cold/offline boot, the two-second dwell, sleep cancellation, wake gesture
suppression and memory headroom.

### Display power saving

Settings → Screen power edits independent battery and USB/external timeouts in
seconds (0–86400; **0 means always on**). Code defaults are **30 seconds on
battery** and **0 on external power**. Optional provisioner values use NVS keys
`controller/sleep_bat_def` and `sleep_usb_def` (u32). Saved user preferences
`sleep_battery` and `sleep_usb` take precedence. Reset defaults removes both
user overrides and restores Auto mode. `power_mode` (u8) stores Auto=0,
Battery=1 or USB/external=2. Values survive reset/power loss; replacing the NVS
partition restores the provisioned state and removes later user changes.

**Automatic USB detection is not implemented. Auto treats the source as unknown
and uses the battery timeout, even with USB connected.** Select USB/external
manually to use its separate timeout. No onboard accelerometer is used. The
published board circuit does not provide an established firmware-readable
USB-present input; charging state and USB data enumeration are not substitutes.

Touch/held touch renews the inactivity timer; metadata, progress, artwork and
network events do not. On expiry only the backlight is switched off: touch,
networking and playback state continue. The whole first touch gesture wakes the
screen without operating a hidden control. This is backlight power saving,
not deep sleep. Changes start a fresh timer; selecting an effective Always on timeout wakes
an already dark screen. Hardware acceptance must verify backlight switching,
first-touch suppression, offline operation and battery consumption on the board.

The gateway's `/controller/settings` page can edit a connected device by its
stable factory-MAC-derived ID. It waits for a correlated acknowledgement after
NVS commit and reports failures/disconnection rather than claiming success.
Device NVS remains authoritative; the gateway neither stores nor queues offline
preferences. Deploy the updated gateway and route `/controller/settings*` to its
`/settings*` paths, preserving the device WebSocket route. Older gateways ignore
the optional state report; local timeout settings still work. Full-flash images
can overwrite NVS: restore your private image afterwards, or use application-only
updates at the documented application offset to retain preferences.

### Provider icons

The top-right icon beside the player identifies the selected browsing/search
provider and opens Providers when tapped. Provider rows use the same icon:
selected is full colour, other providers are grey with muted names. They remain
tappable and exactly one is selected; Save persists the choice as before. The
album-art badge identifies the current stream source, which can differ from the
selected browsing provider. Unknown or ambiguous stream sources have no badge.

The gateway discovers MA logos and serves transparent 40×40 MCPI packets through
the existing token-authenticated artwork route. A separate worker refreshes
registered icons around every 30 seconds without UI locks during networking.
The UI retains at most 24 icon buffers in PSRAM (roughly 113 KiB at 16-bit colour).
Downloads never write NVS. Upload per-instance logo overrides at
`/controller/settings/providers` when MA has no suitable logo, including an
OpenSubsonic instance that is specifically Navidrome. Local UI falls back to a
music symbol before icons load or with an older gateway; other controls and
power-saving timers continue to work. Logo refresh does not count as activity.

Native acceptance uses upstream Spotify/BBC Sounds logos plus a Navidrome override
fixture, checks greyscale selection, exclusive choice, malformed packets, and
independent selected/source badges. Real-device acceptance must confirm header
player-name fit, touch targets, provider discovery on the installed MA version,
upload refresh, source correctness and PSRAM availability.

Provider selection uses dedicated `SELECTION` / `ON_SELECTION` colour roles,
separate from ordinary cards and subtle pressed/active surfaces. The entire
selected row gets the palette's stronger selection fill with white text; the
Grey palette uses blue in both light and dark modes. Unselected rows retain
muted text and grey icons. Shared styles update existing rows on theme changes.
Native acceptance checks distinct selected/unselected fills and at least 4.5:1
selected-caption contrast across all twelve palette/mode combinations, with
representative Green and Grey screenshots in both modes.

### Main-task stack overflow during startup

`***ERROR*** A stack overflow in task main has been detected` during
`Initialize Touch LCD` is an application startup stack failure. The old build
used ESP-IDF's default 3584-byte main-task stack while the application's main
frame also reserved large runtime command buffers. This can fail before the
screen and network initialize. Erasing NVS or flashing the full image does not
correct the application stack budget.

The firmware now explicitly reserves 16384 bytes for the main task and keeps the
runtime loop in a separate non-inlined function so its command buffers are not
resident during hardware initialization. A compile-time guard rejects an existing
`sdkconfig` with a smaller main stack: set
`CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` in menuconfig (or regenerate configuration
from the defaults) before rebuilding. Application component frames larger than
4096 bytes fail the build; compiler `.su` stack reports accompany CI artifacts.
This frame limit does not prove the worst-case nested call-chain stack usage.

Serial logs report configured stack size and the minimum free bytes after
LCD/touch, UI and network initialization, then during runtime. Verify on the
physical board that it gets beyond each phase, shows the UI, connects normally,
and retains stack margin through rendering and playback controls.

For an already provisioned board, close the serial monitor and flash the fixed
application artifact only (substitute your COM port):

```powershell
py -m esptool --chip esp32s3 --port COM4 --baud 460800 write-flash 0x20000 music_controller.bin
py -m serial.tools.miniterm COM4 115200
```

Do not erase flash or reapply NVS for this fix. The application partition, NVS
layout and credentials are unchanged. A successful CI build is not a physical
startup test; retain the new serial logs for hardware verification.
### LVGL memory exhaustion during UI creation

After the stack fix, a boot could reach `Creating diagnostic LVGL screen` and
panic with `LoadProhibited` / `EXCVADDR: 0x00000020`. LVGL's default 32 KiB
private pool is too small for this UI; native reproduction exhausted it while
allocating a widget's local style. Initial UI creation alone used about 73 KiB
on the 64-bit native test host (device sizes differ).

LVGL now uses a custom malloc/realloc/free adapter explicitly backed by PSRAM,
including small widget/style allocations. It shares available PSRAM with the
framebuffers and artwork caches rather than reserving another fixed pool.
Allocation failure logs the requested size and aborts with a useful backtrace.
Startup logs report remaining PSRAM and its largest free block before/after UI
creation. Native acceptance runs use LVGL's same custom allocator path.

For existing local builds regenerate `sdkconfig` from defaults, or set
`CONFIG_LV_MEM_CUSTOM=y` and
`CONFIG_LV_MEM_CUSTOM_INCLUDE="controller_lvgl_memory.h"`. Stale allocator
configuration fails compilation. CI artifacts include the matching `.elf` and
`.map` files so exception addresses can be decoded with
`xtensa-esp32s3-elf-addr2line -pfiaC -e music_controller.elf ADDRESS...`.
Flash only the application at `0x20000` as above to preserve NVS.

### TLS seed failure / SHA buffer allocation failure

`ip=1 ws=0 gateway=0` with `esp-sha: Failed to allocate buf memory` and
`ESP_ERR_MBEDTLS_CTR_DRBG_SEED_FAILED` indicates a local crypto allocation failure
before the gateway WebSocket handshake. The SHA DMA adapter needs internal RAM;
free PSRAM alone cannot satisfy that request.

The controller's bulk UI state, player/provider lists, queue, browse and search
caches now live in zero-initialized external BSS (about 130 KiB on ESP32-S3).
mbedTLS allocations explicitly use PSRAM, while 64 KiB of internal memory is
reserved from ordinary malloc for internal-only allocations such as task stacks
and DMA buffers. This reserve is shared, not a dedicated SHA buffer. Hardware
SHA remains enabled and TLS certificate/time validation remains enabled.

Existing local builds must enable `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`
and `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, and set
`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`, or regenerate sdkconfig.
Connection and heartbeat logs show internal free bytes, largest internal/DMA
blocks, and free PSRAM. Verify successful `gateway/ready` and repeat with artwork,
provider icon requests on the physical board.

### Buffered presentation and startup backlight

All drawing uses LVGL and the pinned BSP's **anti-tearing mode 3 (direct mode)**,
with 90-degree rotation. The rotated path uses one full logical drawing buffer
and two RGB panel buffers in PSRAM (three 768,000-byte buffers, about 2.20 MiB).
The BSP rotates/copies completed dirty regions into the secondary panel buffer,
presents it at VSYNC and synchronizes the other panel buffer for later updates.
This was already selected in defaults; firmware now rejects stale configurations
that disable it or select another mode/rotation.

Rendering rules for future changes:

- Build screen/menu/dialog and substantial content changes in one LVGL transaction:
  on the LVGL task, or under `lvgl_port_lock` from application tasks. Finish the
  entire change before unlocking. Screen navigation invalidates the complete root.
- Keep button feedback, selection, typing, search results and playback progress on
  the same buffered presentation path. Dirty-region rendering avoids repainting
  unrelated artwork; it does not permit writes into the active panel buffer.
- Do not clear the visible panel, draw directly with panel APIs, or manually swap
  buffers from UI code. The BSP owns rotation, buffer synchronization and VSYNC.
- Do not force intermediate refreshes during a transition. In particular,
  `lv_refr_now` must run on the BSP LVGL task: its flush waits for that task's
  VSYNC notification. A mutex alone does not make another task suitable.
- Skip unchanged text updates. Network/metadata ticks must not repeatedly
  invalidate static labels. Download assets outside UI locks and apply complete,
  validated assets inside one transaction.

Startup first presents a minimal “Loading...” page using a one-shot LVGL timer,
then enables the backlight. The application waits for that presentation before
building the main UI, saved theme and initial state in one locked transaction.
The loading frame stays visible until the BSP presents the completed main frame.
This does not wait for Wi-Fi, the gateway or artwork. Later transitions retain
the previous frame rather than inserting a loading page. The board's hardware
reset sequence can still cause a flash before software backlight control exists.

Hardware acceptance: cold boot and reset with/without provisioning or network;
rapid navigation through every view and Screen power; keyboard/results, changing
tracks/albums, provider icons; idle with playback stopped and while
progress advances; timeout and first-touch wake. Look for blank flashes, partial
frames, tearing or panel jitter. Native tests validate UI behavior, not physical
VSYNC timing or RGB DMA bandwidth. Continued flicker needs a short video and
serial logs to distinguish UI invalidation from panel/DMA or power problems.

### Larger touch controls and punctuation

The transport dock contains only Previous, Play/Stop and Next, each occupying
one third of its full 480 × 136 area with 48px symbols. Track details and album
art remain in Playing. The redundant thumbnail buffer has been removed.
The volume strip is 96px wide; its full-width + and - buttons each occupy one
fifth of the strip height (106px), with 48px solid-stroke symbols. The vertical slider and
percentage remain between them. Browse and Search still hide volume and use
all 480px of content width.

The 18px and 24px text fonts render common Unicode curly apostrophes/quotes,
primes, dashes and non-breaking spaces using matching stock punctuation glyphs.
This is a display fallback; stored metadata and UTF-8 search queries are unchanged.
The existing £ search glyph remains available. This does not add general Unicode
alphabet coverage. Local builds must enable `CONFIG_LV_FONT_MONTSERRAT_48=y`
(or regenerate sdkconfig from defaults). Flash the application at `0x20000` to
retain provisioning and preferences. No gateway update or Atlas pin change is
required.

Battery percentage remains unavailable on the unmodified supported 4.3-inch
board. Its [published schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4.3-Sch.pdf)
shows VBAT connected to the CS8501 power circuit, with CHRG/STDBY driving LEDs,
not ESP32 inputs. The Sensor AD header is an external ADC input, not a wired
battery monitor. A reliable battery indicator needs additional sensing hardware,
such as a compatible I²C fuel gauge; no percentage or charging state is inferred
from screen-power mode or USB enumeration. The provider shortcut remains until
real battery telemetry is available.

Physical acceptance: verify large button edge taps, stop/play/skip and volume
stepping on the chosen output; slider dragging; normal and disabled contrast;
Playing/Queue/Settings fit; Browse/Search full width; and a title such as
“Don’t You Remember” without a missing-glyph box. Native acceptance checks layout,
action targets, glyph resolution and artwork handling, not physical touch comfort.
