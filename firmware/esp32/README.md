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
build/native-ui/ui_acceptance artifacts/native-ui
python tests/native-ui/convert_screenshots.py artifacts/native-ui
```

### Appearance and staged navigation

The header cog opens appearance settings. Six palettes (Green, Blue, Red, Orange, Purple, Grey) each support Dark and Light modes. Shared semantic LVGL styles apply to existing controls and newly created player/source rows. Selection is stored as `ui_palette` and `ui_light` in the `controller` NVS namespace and restored on boot. Invalid palette values fall back to Green.

The bottom tabs match the design: Playing, Queue, Browse, Search. All four tabs are functional. Use the header player selector for Players and Settings → Music provider for provider selection.

Native acceptance checks all twelve theme selections and their persistence callbacks once, with a single settings/edit example. Screenshots focus on functional states; theme permutations are no longer captured on every screen. Checks retain fixed-caption fit, navigation, disabled interactions and persistent dock geometry.

### Gateway-served album artwork

The companion Atlas gateway advertises `artwork_rgb565` and annotates current queue items with `controller_art_id`. Firmware fetches `/device/artwork/{id}` from the same provisioned gateway using the existing device token in a dedicated worker. A complete validated MCAR packet contains a 288 × 288 little-endian RGB565 cover. PSRAM holds the transfer and reusable display buffers; the 40 × 40 dock thumbnail is derived locally. Downloads never hold LVGL locks. Results are discarded after a player, queue, track or artwork change. Same-album artwork is reused. Artwork retains its original colours under every theme.

The gateway supplies a generic record cover for absent or failed provider art. An unreachable/older gateway leaves the local placeholder visible, without affecting playback controls. Companion changes are in Atlas PR #614.

Native acceptance mocks gateway packets using `tests/native-ui/mock_artwork.py`, then passes that fixture directory as a second argument to `ui_acceptance`. It checks both artwork slots, malformed/truncated packets, stale responses and artwork changes; functional screenshots include real-cover fixtures and generic covers.

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
