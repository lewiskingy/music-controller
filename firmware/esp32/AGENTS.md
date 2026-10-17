# ESP32 firmware engineering guidance

Read [README.md](README.md), `main/idf_component.yml`, `sdkconfig.defaults` and the
root AGENTS.md before changing firmware. The README's buffered-presentation section
explains the display pipeline and physical acceptance checklist.

## Supported hardware and dependencies

Target the Waveshare ESP32-S3 Touch LCD **4.3-inch 800×480 GT911**, 16 MB flash,
8 MiB PSRAM. Other similarly named boards are not interchangeable. The interface
is portrait 480×800 using BSP 90-degree rotation for both display and touch.

The current baseline is ESP-IDF 5.4.2, BSP
`bobscott45/waveshare_esp32_s3_touch_lcd_4_3` 1.0.7 and LVGL 8.3.11. Treat the
manifest and workflow as authoritative. Inspect the pinned dependency's source
before relying on driver behavior; do not assume newer documentation applies.

## Rendering and task ownership

- Preserve BSP anti-tearing mode 3, direct rendering and 90-degree rotation.
  Its rotated path uses a logical drawing buffer and two RGB panel buffers in
  PSRAM, with VSYNC presentation. Preserve stale-sdkconfig compile guards.
- Build a screen/menu/dialog transition or substantial content replacement in one
  LVGL transaction. Work on the LVGL task or hold `lvgl_port_lock` throughout the
  application-side change. Finish all widget changes before unlocking.
- Navigation invalidates the full root. Small control, typing and progress updates
  use dirty regions through the same buffered path. Skip unchanged label values;
  do not repaint static artwork or rebuild lists on every metadata tick.
- Never write directly to the displayed framebuffer, clear the panel from UI code
  or manually swap buffers. The BSP owns rotation, buffer synchronization and
  presentation. Do not force intermediate refreshes during a transition.
- `lv_refr_now` must run on the BSP LVGL task: the flush waits for that task's
  VSYNC notification. Holding the mutex on another task is insufficient.
- Keep the bundled Loading splash visible while preparing the initial main frame.
  Later transitions retain the previous frame until the replacement is ready.
  Do not wait for network access or artwork before showing useful startup UI.

## Memory, concurrency and state

- Keep bulk UI state, image caches and LVGL allocations in PSRAM. Preserve the
  internal-memory reserve for internal-only DMA and task-stack allocations.
  Check allocation failures before use; keep transfers and caches bounded.
- Avoid large local arrays and oversized call frames. Extract coherent operations
  into procedures rather than growing startup/runtime functions. Retain stack
  high-water and internal/DMA/PSRAM diagnostics and ELF memory-layout validation.
- Do not call LVGL from Wi-Fi/WebSocket callbacks. Publish state for the rendering
  path. Perform downloads outside UI/state locks, then apply complete validated
  assets in one UI transaction. Respect existing lock ordering; avoid network
  waits or long work under locks.
- Retain request/generation/identity checks for search, browse, artwork and playback.
  A late response must not overwrite a newer screen or control another player.
  Music Assistant remains authoritative for playback state.
- Keep UI procedures independent of networking and NVS where practical. Extract
  host-testable policy helpers for reusable state/command decisions. Reuse shared
  styles, controls and rendering helpers instead of duplicating per-screen logic.

## Power, security and persistence

Metadata, progress, network and artwork events do not count as user activity.
Consume the complete first wake gesture without operating a hidden control.
Auto power mode currently uses the battery timeout because reliable USB-present
hardware detection is not implemented. Do not infer USB power from charging or
USB enumeration without verified board support.

Keep Music Assistant account credentials off the device. Preserve certificate/time
validation and the separate device token. Never publish `controller-nvs.bin`.
For provisioned boards prefer application-only flashing at `0x20000`; full merged
images can overwrite NVS. Do not instruct an erase/reprovision for an ordinary
application change. Preserve preference keys or provide an explicit migration.

## Validation

From the repository root, run the relevant firmware contract suite:

```sh
python -m pytest -q tests/test_esp32_power.py tests/test_playback_policy.py tests/test_esp32_focus_controls.py tests/test_esp32_providers_volume.py tests/test_esp32_provider_save.py
```

For UI changes, build/run native acceptance against LVGL 8.3.11:

```sh
cmake -S tests/native-ui -B build/native-ui -DLVGL_SOURCE_DIR=/absolute/path/to/lvgl
cmake --build build/native-ui -j2
python tests/native-ui/mock_artwork.py build/mock-artwork
mkdir -p artifacts/native-ui
build/native-ui/ui_acceptance artifacts/native-ui build/mock-artwork
```

Use the gateway requirements for artwork-fixture dependencies. Follow
`.github/workflows/firmware.yml` for the ESP-IDF build, ELF memory-layout check and
artifact packaging. Tests should verify observable behavior, including stale
responses and unnecessary redraws, rather than merely mirror implementation text.

Native acceptance cannot prove physical VSYNC, RGB DMA bandwidth, touch alignment,
battery behavior or TLS memory headroom. Identify hardware checks still needed:
cold boot, offline startup, navigation/dialogs, typing/search, track/artwork
changes, boot/sleep splash, stopped/playing idle, sleep/wake and network
reconnection. Keep physical acceptance claims separate from compilation and simulator results.
