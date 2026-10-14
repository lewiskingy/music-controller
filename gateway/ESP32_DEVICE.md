# ESP32 device connection

The gateway accepts native devices at `/device/ws`. The example TLS frontend exposes it as `wss://YOUR_HOST/controller/device/ws`. Firmware supplies `X-Device-Token` from private NVS; the gateway matches it against `CONTROLLER_DEVICE_TOKEN` before authenticating upstream to Music Assistant.

1. Configure/start the gateway as described in [README](README.md), including a random device token.
2. Supply a certificate-verified WSS frontend and DNS that resolves to your server from the device's Wi-Fi network.
3. Follow [firmware build/flash/provisioning](../firmware/esp32/README.md). Enter your WSS URL explicitly: the provisioner retains an Atlas-specific convenience default for existing devices, which other installations must replace.
4. Enter the same gateway device token, your Wi-Fi settings and optionally a Music Assistant player ID. The token is independent of MA credentials.
5. Flash full firmware at `0x0`, then private `controller-nvs.bin` at `0x9000`.

A full merged flash image may overwrite NVS; always reflash private NVS afterwards. Do not share NVS files: they contain credentials. Rotate the server token and reprovision devices if it is disclosed. Server credentials never belong on the device.

Verify real metadata, player selection, transport/volume, provider Save across reset and reconnection after gateway/Wi-Fi interruption. Check serial logs for authentication/connection errors. TLS problems usually need DNS, certificate trust or clock/network investigation; do not disable certificate verification. The gateway health endpoint does not test any of these device conditions.

## Pending portrait firmware (music-controller PR #20)

This gateway includes Atlas PR #614's capability event, seeking, stable queue playback, bounded Browse/Search and authenticated artwork. Keep the same provisioned WSS endpoint and token. The portrait firmware derives its artwork HTTPS URL by replacing the endpoint's final `/ws` with `/artwork/{id}`; your reverse proxy must route that sibling path to gateway `/device/artwork/{id}`. The bundled Caddyfile includes both routes. No Wi-Fi/device credential reprovisioning is required just to update the gateway.

The original main-branch firmware continues to work but does not gain portrait UI/artwork/Search by a gateway update alone. Neither PR #20 nor Atlas PR #614 is merged/deployed by this migration. See the [expanded contract](../contracts/music-assistant.md) and validate live Music Assistant providers and physical touch after deployment.
