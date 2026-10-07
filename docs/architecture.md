# Architecture

## Responsibilities

| Layer | Owns | Does not own |
| --- | --- | --- |
| Music Assistant | Provider catalogue, search, queue, playback orchestration, player discovery, streaming | Controller-specific UI |
| Browser prototype | Touch navigation, view state, MA API calls and event subscription | Media catalogue persistence, transcoding, Sonos/Cast protocols |
| ESP32 firmware | LVGL presentation, Wi-Fi, MA client, touch, sleep, OTA | Independent media server |
| Atlas | Music Assistant hosting, optional prototype hosting, OTA artifact distribution and LAN firewall | Product UI and firmware source |

## Runtime topology

```text
                        Music Assistant (Atlas)
                         /                 \
              HTTPS/WS or HTTP/WS     HTTP/WS on LAN
                     /                       \
           Browser prototype             ESP32-S3
          (served on Atlas)            ESP-IDF + LVGL
```

The browser prototype is not an executable image for the ESP32. Its **interaction model, navigation, command/state contract and visual design** are the reusable outputs.

## Direct API principle

The default is direct communication from each client to Music Assistant's supported authenticated API and events. During the API spike, verify:
- Current supported authentication/token flow for non-browser embedded clients.
- Whether a static browser origin can connect without CORS problems.
- API method names and payloads from the installed MA version.
- WebSocket reconnection, initial snapshots, state deltas and command acknowledgements.
- Image resizing, memory use and cache policy.
- Player capabilities and supported Pause/Stop semantics per protocol.

If browser cross-origin restrictions require a reverse proxy, prefer same-origin proxying rather than introducing a bespoke control service. Do not expose MA tokens in public source or checked-in settings.

## Playback and device semantics

Music Assistant is the authoritative source for queue and player state. The controller must reflect externally initiated changes, not optimistically assume commands succeeded. A selected player can be Sonos, Cast or another MA-supported player; commands must respect its capabilities.

Known follow-up: [Atlas #587](https://github.com/lewiskingy/atlas/issues/587) documents unexpected queue hopping on Pause with a Sonos player. Do not hard-code an unverified workaround in the controller.

## Firmware and updates

ESP32 firmware boots from local flash. Atlas may publish an approved manifest and signed firmware artifacts. OTA should use inactive partitions, integrity/signature verification, rollback and idle/docked apply policy. No PXE dependency or network-critical boot path.

## Deployment boundary

This repository owns the client and firmware. Atlas owns Docker, Caddy, firewall and hosting changes. Keep deployments separate and reference the originating [Atlas #577](https://github.com/lewiskingy/atlas/issues/577).
