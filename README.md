# Music Controller

A shared household touchscreen remote for [Music Assistant](https://www.music-assistant.io/). Select an output player, see what is playing and control music without opening a phone app. The target device is a Waveshare ESP32-S3 4.3-inch 800×480 capacitive touchscreen.

This repository contains the **ESP32 firmware, required backend gateway and browser UX prototype**. You can build and run them with your own Music Assistant server; Atlas is one deployment, not a prerequisite.

## What works today

| Part | Current capabilities |
| --- | --- |
| ESP32 firmware | LCD/touch, Wi-Fi, certificate-verified WSS, live player/queue metadata, player selection, playback controls, 5% volume stepping, provider discovery and selection saved to NVS |
| Gateway | Server-side Music Assistant login, device/browser admission checks, validated playback/seek commands, stable queue playback, compact Browse/Search adapters, authenticated RGB565 artwork and live event relay |
| Browser prototype | Now Playing, player selection, transport, volume, queue selection and catalogue browsing; an interaction prototype rather than ESP32 firmware |

Native artwork, Queue/Browse/Search screens, provider-filtered browsing/search, power management and OTA delivery remain follow-up work on the current main branch. This gateway also includes the support required by the unmerged [portrait firmware PR #20](https://github.com/lewiskingy/music-controller/pull/20), copied from [Atlas PR #614](https://github.com/lewiskingy/atlas/pull/614); merging the gateway does not itself add those native UI features. The browser Search view is a placeholder. Provider selection on the device does not yet filter catalogue requests. Sonos Pause is disabled while playing because of observed queue hopping; Stop is available.

## Architecture

The ESP32 and browser connect to the gateway. It authenticates to Music Assistant using a dedicated account and forwards only supported commands and state/events. Music Assistant owns providers, catalogue, queues, audio streaming and output-player integration. Audio goes from Music Assistant to the player, not through this gateway or the ESP32.

Music Assistant credentials stay on the gateway host. The ESP32 receives a separate device token; Wi-Fi settings, endpoint and token are provisioned locally into NVS. An HTTPS reverse proxy terminates TLS for the device and serves the browser prototype on the same origin as its WebSocket endpoint.

See [architecture](docs/architecture.md) and the [gateway protocol contract](contracts/music-assistant.md).

## Repository contents


| Path | Purpose and usage |
| --- | --- |
| [gateway/](gateway/README.md) | Python/aiohttp gateway, Dockerfile, Compose example, optional Caddy frontend, environment template and integration tests |
| [firmware/esp32/](firmware/esp32/README.md) | ESP-IDF/LVGL application, board dependencies, partitions and local NVS provisioning tool; build/flash the physical controller |
| [prototype/](docs/prototype.md) | Static HTML/CSS/JavaScript controller; inspect the touch UX or use it with the gateway |
| [contracts/](contracts/music-assistant.md) | Shared authentication, requests, responses and supported command boundary |
| [docs/](docs/architecture.md) | Architecture, hardware, browser operation and development/CI guidance |
| [hardware/](hardware/README.md) | Hardware direction and specifications |
| [hardware/enclosure/](hardware/enclosure/README.md) | Printable portrait enclosure/dock CAD, assembly guide, geometry checks and independent build/render workflow |
| tests/test_esp32*.py | Firmware source-contract checks; CI also builds the real ESP-IDF application |
| tests/prototype/ | Browser single-screen/layout contract checks |
| .github/workflows/ | Separate firmware, gateway and prototype checks, plus enclosure CAD checks and explicit tagged prototype/hardware releases |

## Set up your own system

1. Have a working Music Assistant server and at least one output player. Create a dedicated non-admin Music Assistant user for the controller.
2. Follow [gateway setup](gateway/README.md): copy `.env.example`, configure your server/account, generate a device token, and build/start the gateway. Supply a trusted HTTPS/WSS frontend for a physical device.
3. Follow [firmware installation](firmware/esp32/README.md): build or download a successful Actions artifact, provision your Wi-Fi, **your** gateway WSS URL and the matching device token, then flash firmware followed by private NVS.
4. Optionally use the [browser prototype](docs/prototype.md) through the example same-origin frontend.

No Atlas checkout, Atlas secrets path, deployment script or home domain is required. Existing Atlas deployment stays unchanged by this migration.

## Fundamental UX principle — one focused screen

Design for a strict **800 × 480** landscape viewport: Now Playing, Players, Providers, Queue, Browse or Search replaces the previous primary screen. The outer page/device never scrolls; only bounded lists scroll. Keep controls touch-friendly with one obvious way back. Apply this principle to both the prototype and native firmware.

## Development, tests and releases

See [development and CI](docs/development.md) for commands and the change-to-workflow matrix. Gateway-only commits do not build ESP firmware; firmware-only commits do not build the gateway. Shared `contracts/` changes run all three component checks. Tagged `vX.Y.Z` releases package the static browser prototype only; firmware Actions artifacts and the locally built gateway container are separate outputs.

Keep `.env` and `controller-nvs.bin` private. The browser origin check is not user authentication: deploy the controller on a trusted LAN and restrict access there. Never expose the example frontend or Music Assistant ports through public router forwarding.

Originating initiative: [Atlas #577](https://github.com/lewiskingy/atlas/issues/577).

### Firmware display rendering

The ESP32 UI uses VSYNC-synchronized buffered presentation for all updates, with
whole-frame navigation and dirty-region control feedback. Startup shows a minimal Loading page while the main UI is prepared off-screen. See the firmware README's
[rendering rules and hardware acceptance](firmware/esp32/README.md#buffered-presentation-and-startup-backlight)
before changing display, transition or refresh code.
