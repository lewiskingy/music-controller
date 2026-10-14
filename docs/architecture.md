# Architecture

## Responsibilities

| Component | Responsibility |
| --- | --- |
| Music Assistant | Provider accounts, catalogue/search, queues, player discovery and audio streaming |
| Gateway | Authenticate to Music Assistant, validate a limited command surface, relay replies/events; independently runnable from this repo |
| ESP32 firmware | LVGL presentation, touch actions, Wi-Fi/WSS recovery and locally provisioned configuration |
| Browser prototype | Touch interaction prototype and same-origin WebSocket adapter |
| Deployment host / reverse proxy | HTTPS certificates, LAN access controls, credentials, gateway container and optional static hosting |
| Atlas | One existing host/deployment; its implementation and operational configuration are unchanged |

## Connections and trust

The physical controller opens WSS to `/controller/device/ws` on a TLS frontend. The frontend rewrites to the gateway's `/device/ws`; the firmware sends `X-Device-Token` from NVS. The gateway requires an exact match to `CONTROLLER_DEVICE_TOKEN` before connecting upstream.

The browser is served at `/controller/` and opens `/controller/ma/ws`, rewritten to gateway `/ws`. The gateway checks `Origin` against configured `CONTROLLER_ORIGIN`. This prevents other browser origins using that endpoint, but is not authentication for arbitrary LAN clients. Access belongs behind trusted-LAN network controls.

Each accepted client session gets its own Music Assistant WebSocket connection. The gateway performs `auth/login` and `auth`, keeps the upstream token server-side, then sends `gateway/ready`. Validated requests and Music Assistant replies/events are relayed. Disconnection ends that session; clients reconnect and fetch fresh state.

The optional example frontend maps `/controller/ma/image` to Music Assistant `/image` for browser artwork. It does not relay general Music Assistant APIs. Image access depends on the installed Music Assistant version and may fall back to placeholders. Current main firmware does not yet fetch artwork. The gateway also serves token-authenticated `/device/artwork/{art_id}` for pending portrait firmware PR #20; the example frontend maps `/controller/device/artwork/{art_id}` to it.

## State and command ownership

Music Assistant is authoritative for playback, volume and queues, including externally initiated changes. The controller presents reported state rather than assuming an action succeeded. Device player/provider preferences live in local NVS; they are not another catalogue or queue.

Audio flows directly from Music Assistant to Sonos/Cast/other players. Gateway WebSockets carry control and metadata only. Music Assistant's streaming-port reachability is a separate deployment concern.

The browser prototype is not a firmware image. Its navigation/visual model and protocol adapter inform the LVGL implementation. Current device features are listed in the firmware README; browser feature parity is not implied.

## Deployment and migration

Gateway source was copied from Atlas commit `630a5fe439a7b438a9381dd9a692550a247e91db`, directory `docker/proxies/music-controller-gateway`, then updated with the gateway changes and tests from unmerged Atlas PR #614 at `ef2979d0332a7aa1673c26ada1a6fd345e2b04ff`. These changes support the pending portrait firmware in music-controller PR #20. PR #21 is enclosure-only and is now merged; other open Atlas PRs #611, #602 and #603 have no gateway changes. This repo owns the new reusable copy and its tests/documentation. Atlas was deliberately left alone: no service build context, proxy, firewall, secrets or deployment references were changed. Switching Atlas to this source/container is a separate future change. Until then the two copies do not synchronise automatically.

Changes for independent operation are configurable browser origin, a generic login device name, dependency/build/deployment files, a reusable application factory and handling invalid JSON/non-object requests without breaking the relay. The device-token protocol and existing commands are preserved; PR #614 adds seek, stable queue-item IDs, bounded Browse/Search adapters, capability advertisement and authenticated artwork. The complete imported behavior is covered by its original mocked tests alongside standalone migration tests.

## Future device updates

The partition table reserves two OTA slots. Automated/signed OTA delivery, rollback validation and idle/docked application policy are planned, not implemented. Firmware boots from local flash. Current full-image USB updates must be followed by private NVS provisioning; application-only updates can preserve NVS when flashed at the application offset.
