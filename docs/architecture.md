# Architecture

## Responsibilities

| Component | Responsibility |
| --- | --- |
| Music Assistant | Provider accounts, catalogue/search, queues, player discovery and audio streaming |
| Gateway | Authenticate to Music Assistant, validate a limited command surface, relay replies/events; independently runnable from this repo |
| ESP32 firmware | LVGL presentation, touch actions, Wi-Fi/WSS recovery and locally provisioned configuration |
| Browser prototype | Touch interaction prototype and same-origin WebSocket adapter |
| Deployment host / reverse proxy | HTTPS certificates, LAN access controls, credentials, gateway container and optional static hosting |
| Atlas | One deployment consumer of versioned gateway source; not required for other installations |

## Connections and trust

The physical controller opens WSS to `/controller/device/ws` on a TLS frontend. The frontend rewrites to the gateway's `/device/ws`; the firmware sends `X-Device-Token` from NVS. The gateway requires an exact match to `CONTROLLER_DEVICE_TOKEN` before connecting upstream.

The browser is served at `/controller/` and opens `/controller/ma/ws`, rewritten to gateway `/ws`. The gateway checks `Origin` against configured `CONTROLLER_ORIGIN`. This prevents other browser origins using that endpoint, but is not authentication for arbitrary LAN clients. Access belongs behind trusted-LAN network controls.

Each accepted client session gets its own Music Assistant WebSocket connection. The gateway performs `auth/login` and `auth`, keeps the upstream token server-side, then sends `gateway/ready`. Validated requests and Music Assistant replies/events are relayed. Disconnection ends that session; clients reconnect and fetch fresh state.

The optional example frontend maps `/controller/ma/image` to Music Assistant `/image` for browser artwork. It does not relay general Music Assistant APIs. Image access depends on the installed Music Assistant version and may fall back to placeholders. Current firmware fetches token-authenticated RGB565 artwork through gateway `/device/artwork/{art_id}`; the example frontend maps `/controller/device/artwork/{art_id}` to it.

## State and command ownership

Music Assistant is authoritative for playback, volume and queues, including externally initiated changes. The controller presents reported state rather than assuming an action succeeded. Device player/provider preferences live in local NVS; they are not another catalogue or queue.

Audio flows directly from Music Assistant to Sonos/Cast/other players. Gateway WebSockets carry control and metadata only. Music Assistant's streaming-port reachability is a separate deployment concern.

The browser prototype is not a firmware image. Its navigation/visual model and protocol adapter inform the LVGL implementation. Current device features are listed in the firmware README; browser feature parity is not implied.

## Deployment ownership

This repository is the source of the reusable gateway, firmware, contracts and enclosure. Atlas consumes a pinned gateway release separately; it is not part of the setup required by this repository. New unified releases use `vX.Y.Z`; historical `gateway-v…` tags remain available. Consumers build gateway images from source rather than pulling a published registry image.

Device preferences are authoritative in local NVS. The gateway forwards settings changes only to online controllers and waits for acknowledgement; it does not queue offline changes. Provider-logo overrides are gateway state, persisted in its `/data` volume.

## Future device updates

The partition table reserves two OTA slots. Automated/signed OTA delivery, rollback validation and idle/docked application policy are planned, not implemented. Firmware boots from local flash. The Windows installer supports settings-preserving USB updates and complete new-device provisioning. Manual full-image USB flashes must be followed by private NVS restoration; application-only updates preserve NVS when the partition layout matches.
