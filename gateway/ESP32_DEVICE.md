# ESP32 device connection

Follow [Getting Started](../docs/getting-started.md) for a complete deployment and installation. The gateway accepts native devices at `/device/ws`; the example TLS frontend exposes `wss://YOUR_HOST/controller/device/ws`.

Firmware supplies `X-Device-Token` from private NVS. The gateway compares it with `CONTROLLER_DEVICE_TOKEN` before logging in to Music Assistant. Music Assistant account credentials remain on the gateway host.

## Required proxy routes

| Public path | Gateway path |
| --- | --- |
| `/controller/device/ws` | `/device/ws` with WebSocket upgrades and device-token header preserved |
| `/controller/device/artwork/*` | `/device/artwork/*` with device-token header preserved |
| `/controller/settings*` | `/settings*` for trusted-LAN browser settings and provider-logo uploads |

The artwork URL is derived from the provisioned endpoint's final `/ws`; keep the sibling artwork route. The [bundled Caddyfile](Caddyfile) implements these routes. Preserve browser Origin for settings writes and restrict ingress to trusted networks.

## Provision and verify

Use the [Windows installer](../installer/README.md) with your Wi-Fi, certificate-verified WSS URL and matching controller key. For manual Actions artifacts use the [firmware guide](../firmware/esp32/README.md): full merged firmware at `0x0`, then private NVS at `0x9000`. Tagged release application binaries are not merged images. Ordinary installer updates preserve NVS.

Verify real metadata, selected-player transport/volume, Queue/Browse/Search, artwork, provider Save across reset and reconnection after gateway/Wi-Fi interruption. The health endpoint does not test MA login or these device conditions. See [troubleshooting](../docs/troubleshooting.md).
