# Music Controller

A compact, household-friendly touchscreen remote for [Music Assistant](https://www.music-assistant.io/), initially targeting a Waveshare ESP32-S3 4.3-inch 800×480 capacitive display.

**Status:** architecture and browser-prototype foundation. Hardware firmware has not been implemented.

## Goals

- One shared household remote for Music Assistant music libraries and players.
- Browse, search, inspect queues, select rooms, and control playback.
- Keep Music Assistant responsible for providers, catalogue, streaming, queues and player protocols.
- Validate the 800×480 touch experience in a browser before porting the UI to ESP-IDF/LVGL.
- Support a docked/battery-powered appliance with Atlas-managed OTA updates in a later phase.

## Architecture

```text
Navidrome / Spotify / BBC / other providers
                  |
           Music Assistant
             /         \
      Web prototype   ESP32-S3 firmware (later)
      (browser)        (ESP-IDF + LVGL)
```

Both clients should use the Music Assistant API directly where feasible. A proxy or adapter must only be introduced if authentication, browser-origin or embedded resource constraints require one. No duplicate catalogue, queue or player-control backend.

## Repository layout

- [docs/architecture.md](docs/architecture.md) — system boundaries and deployment approach
- [docs/hardware.md](docs/hardware.md) — target device and power/dock direction
- [contracts/music-assistant.md](contracts/music-assistant.md) — API spike and state/command contract
- [prototype/](prototype/) — static HTML/CSS/JS touch UX prototype
- [firmware/esp32/](firmware/esp32/) — future ESP-IDF/LVGL implementation
- [hardware/](hardware/) — future enclosure and dock design

## Run the prototype

From the repository root:

```sh
python3 -m http.server 8080 --directory prototype
```

Open `http://localhost:8080`. The prototype currently runs with **sample state only**; no live Music Assistant API integration is claimed. The next task is to validate authentication and the actual API/event protocol before wiring live commands.

## Next milestones

1. Music Assistant API spike: authenticate, list players, current playback/queue, browse/search, artwork, transport commands and live events.
2. Live web prototype at 800×480 with household usability testing.
3. Waveshare hardware bring-up and native LVGL UI.
4. Power management, dock, enclosure and Atlas-managed signed OTA/rollback.

Originating initiative: [Atlas #577](https://github.com/lewiskingy/atlas/issues/577).

## Security

Never commit Music Assistant credentials, tokens, home Wi-Fi secrets or local host addresses. Use a LAN-restricted deployment and avoid exposing player stream endpoints publicly.
