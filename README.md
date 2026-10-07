# Music Controller

A compact, household-friendly touchscreen remote for [Music Assistant](https://www.music-assistant.io/), initially targeting a Waveshare ESP32-S3 4.3-inch 800×480 capacitive display.

**Status:** LAN-hosted browser prototype with Music Assistant Now Playing, transport, queue and catalogue integration. ESP32 firmware has not been implemented.

## Goals

- One shared household remote for Music Assistant music libraries and players.
- Browse, search, inspect queues, select rooms, and control playback.
- Keep Music Assistant responsible for providers, catalogue, streaming, queues and player protocols.
- Validate the 800×480 touch experience in a browser before porting the UI to ESP-IDF/LVGL.
- Support a docked/battery-powered appliance with Atlas-managed OTA updates in a later phase.

## Fundamental UX principle — one focused screen

**The controller is an appliance, not a scrolling website.** Design for a strict **800 × 480 landscape viewport** matching the Waveshare hardware. Only **one primary viewpoint** may be visible at any time: Now Playing, Queue, Browse, Search or Players. Navigation **replaces** the current view; never append another panel beneath it. A clear Back action returns to Now Playing (or the parent browse level). The page and outer device never scroll; only a bounded list within the active screen may scroll. Controls must remain touch-friendly and useful without browser chrome, zoom or a keyboard. Apply this contract equally to the HTML prototype and future LVGL firmware.

**Acceptance checks:** At 800 × 480, document-level scroll width and height are zero; switching screens does not change the device bounds; Queue and Browse fill the available content area; the Now Playing artwork/transport are not simultaneously displayed behind Queue; the current screen has one obvious navigation path home. Search may initially be a full-screen placeholder until API support is implemented.

## Architecture

```text
Navidrome / Spotify / BBC / other providers
                  |
           Music Assistant
             /         \
      Web prototype   ESP32-S3 firmware (later)
      (browser)        (ESP-IDF + LVGL)
```

The browser currently connects through an **Atlas-hosted, LAN-only, server-authenticated Music Assistant gateway**. Credentials remain in Atlas secrets, not the public repository or browser. The future ESP32 can reuse this interface contract. Neither client owns a separate catalogue, queue or media backend.

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

Open `http://localhost:8080` to inspect layout; live data requires Atlas Caddy routes and the gateway at `https://music.theflat.me.uk/controller/` on the LAN. A local static server alone cannot access that same-origin gateway.

## Next milestones

1. Validate transport, queue and browse commands against the installed Music Assistant version; complete Search and artwork handling.
2. Verify the strict 800×480 single-screen UX on mobile and Waveshare-sized display.
3. Waveshare hardware bring-up and native LVGL UI.
4. Power management, dock, enclosure and Atlas-managed signed OTA/rollback.

Originating initiative: [Atlas #577](https://github.com/lewiskingy/atlas/issues/577).

## Security

Never commit Music Assistant credentials, tokens, home Wi-Fi secrets or local host addresses. Use a LAN-restricted deployment and avoid exposing player stream endpoints publicly.
