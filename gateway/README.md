# Music Controller gateway

The companion backend for ESP32 firmware and the browser prototype. It uses Python 3.12/aiohttp to authenticate to your Music Assistant server and relay a limited set of requests and live events. It does not serve audio or own a music library.

Copied from Atlas main commit `630a5fe439a7b438a9381dd9a692550a247e91db` and updated with unmerged [Atlas PR #614](https://github.com/lewiskingy/atlas/pull/614) at `ef2979d0332a7aa1673c26ada1a6fd345e2b04ff`; Atlas remains unchanged. The newer seek, stable Queue playback, Browse/Search and artwork contracts support [music-controller PR #20](https://github.com/lewiskingy/music-controller/pull/20). Older firmware keeps using its existing commands; new native UI features require that firmware PR. See [architecture](../docs/architecture.md) for migration scope and [protocol](../contracts/music-assistant.md) for the API.

## Requirements

- Docker with Compose v2, or Python 3.12 for local development.
- A Music Assistant server that supports the existing `auth/login` → `auth` WebSocket flow and commands in `app.py`.
- A dedicated non-admin Music Assistant account and network access from the container to its WebSocket port (commonly 8095).
- For ESP32 use, a trusted HTTPS/WSS frontend reachable over Wi-Fi. The physical device requires certificate verification; plain WS is only suitable for local browser/development checks.

No specific Music Assistant release is certified by this copy. Validate authentication and commands against your installed server before relying on them; unit/integration tests use a simulated server.

## Configure and start

From the repository root:

```sh
cd gateway
cp .env.example .env
chmod 600 .env
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
```

Edit `.env`, replace the password/token placeholders and choose your Music Assistant URL. Put the generated token in `CONTROLLER_DEVICE_TOKEN` and later enter the same token into the firmware provisioner. Never use the MA password as the device token.

| Variable | Meaning |
| --- | --- |
| MA_WS_URL | Complete upstream WebSocket URL, e.g. `http://host.docker.internal:8095/ws`; `ws://`/`wss://` also supported by aiohttp |
| MA_USERNAME / MA_PASSWORD | Dedicated Music Assistant account; required at startup |
| CONTROLLER_DEVICE_TOKEN | Random shared device credential; empty disables the device endpoint when running Python directly |
| CONTROLLER_ORIGIN | Exact browser origin, e.g. `https://controller.example.net`, no trailing slash; empty disables browser access |
| CONTROLLER_HOST | Hostname for the optional Caddy frontend; must match the origin and resolve to your host |
| MA_HTTP_UPSTREAM | Host:port for the optional browser image route; usually the same Music Assistant host as MA_WS_URL |
| CONTROLLER_BIND_IP | Host interface for the optional HTTPS frontend; default loopback, set to your server's LAN address for devices |

`host.docker.internal` is mapped to the Docker host by the example Compose file. For Music Assistant on another machine use that machine's DNS name/address instead. If your firewall blocks Docker-to-host connections, permit only the gateway's network/address to the MA port; a healthy container alone does not prove upstream connectivity.

```sh
docker compose config --quiet
docker compose up -d --build gateway
curl --fail http://127.0.0.1:8080/health
docker compose logs --tail 50 gateway
```

The raw gateway port is bound to loopback for use by an existing host reverse proxy. It is not a TLS server. `/health` checks only whether MA account settings are present; it does not log in or prove Music Assistant is reachable. Login happens when a client connects.

## HTTPS frontend and browser hosting

Use your existing LAN reverse proxy, or the optional `web` profile:

```sh
docker compose --profile web config --quiet
docker compose --profile web up -d --build
```

Set a real `CONTROLLER_HOST`, matching `CONTROLLER_ORIGIN`, and `CONTROLLER_BIND_IP` first. The example frontend mounts this checkout's `prototype/` and maps:

| Public path | Destination |
| --- | --- |
| /controller/ | Static browser files |
| /controller/ma/ws | Gateway `/ws` |
| /controller/device/ws | Gateway `/device/ws` |
| /controller/device/artwork/{art_id} | Gateway `/device/artwork/{art_id}` with device-token authentication |
| /controller/ma/image | Music Assistant `/image` (browser artwork) |

The example uses standard Caddy automatic HTTPS, which needs valid DNS and successful certificate issuance. A LAN-only server may require you to configure DNS challenges using an appropriate Caddy build, or supply an existing publicly trusted certificate through your reverse proxy. Do not open the controller to the internet to make this work. Caddy's local CA is not trusted by the firmware's default ESP-IDF CA bundle. Prefer an existing trusted TLS frontend if you already have one. Ports 80/443 must be free on the chosen bind address; existing Caddy users can reuse the routes rather than start another frontend.

For an existing proxy on the host, proxy to `127.0.0.1:8080`; another container must share the gateway's network and use `gateway:8080`. Preserve `Origin` and `X-Device-Token`, support WebSocket upgrades, and apply LAN restrictions at the ingress/firewall. [Caddyfile](Caddyfile) illustrates rewrites; adapt its upstream address for your deployment.

Open `https://YOUR_HOST/controller/` for the browser. Provision the ESP32 with `wss://YOUR_HOST/controller/device/ws`. The device uses this URL from NVS, not a compiled-in server address. See [device setup](ESP32_DEVICE.md).

## Verify operation

1. Missing/wrong device tokens must receive HTTP 403 before the WebSocket upgrade.
2. A valid device token should receive `gateway/ready`, then a `players/all` reply. If login fails, expect `gateway/error` and a closed session.
3. The browser must be served from exactly `CONTROLLER_ORIGIN`; a different origin is rejected.
4. Check actual player metadata, transport and recovery after restarting the gateway. Test commands on the installed MA version; Sonos Pause remains disabled in the clients.

Secrets stay in the gateway environment and private device NVS; never commit them. Docker administrators can inspect container environment. The initial device endpoint shares one token across provisioned devices: rotate the server token and reprovision all devices if it is disclosed. Origin checks do not replace LAN access controls. Browser artwork can fall back to placeholders if MA's image endpoint requires additional authentication.

## Local development and tests

From the repository root:

```sh
python3 -m venv /tmp/music-controller-gateway-venv
. /tmp/music-controller-gateway-venv/bin/activate
python -m pip install -r gateway/requirements.txt pytest==8.4.2
python -m pytest gateway/tests -q
```

To run without Docker, provide the same environment variables through your local process manager/environment and run `python gateway/app.py`. It listens on `0.0.0.0:8080`, so restrict host access appropriately. The `.env` file is consumed by Compose; Python does not load it automatically.

Build only the gateway image with `docker build -t music-controller-gateway:local gateway`. CI tests and builds this component independently of firmware. No registry image publication is configured yet; release consumers build from a pinned checkout.

## Expanded device capabilities (PR #20 compatibility)

After `gateway/ready`, the gateway emits `gateway/capabilities` advertising `seek`, `queue_item_play`, `browse`, `search` and `artwork_rgb565`. The old native firmware ignores unsupported capabilities; the pending portrait firmware uses them to enable its controls. Deploy this gateway before using that firmware, with the same URL/token and no NVS credential changes.

- Seek accepts integer seconds through `player_queues/seek`.
- Queue playback accepts stable item IDs as well as legacy numeric positions.
- `controller/browse` translates fixed catalogue/detail/folder requests into compact pages of at most 20 rows.
- `controller/search` accepts one provider and one media type, normalizes query case, and returns compact rows. An explicit unsupported `providers` argument permits one retry within the same provider's indexed library, labeled `scope: library`; it never switches to all-provider search.
- Current queue items receive opaque `controller_art_id` values. Devices fetch fixed 288×288 RGB565 packets through `/device/artwork/{art_id}` with the same token. Missing/failing images get a generic cover; requests cannot supply arbitrary image URLs. Pillow is pinned in `requirements.txt` for bounded image conversion.

The [protocol contract](../contracts/music-assistant.md) gives bounds, packet format and routing. These changes retain PR #614's mocked acceptance tests; live provider/device validation is still pending. If you use an existing reverse proxy rather than this example, add the artwork route as well as the device WebSocket route.

### Optional controller backdrop

Open `/backdrop` on the gateway, or `/controller/backdrop` through the example
Caddy frontend. This trusted-LAN page requires no login. Upload JPEG, PNG or WebP
(up to 8 MiB and 4 megapixels), preview the centre crop, set brightness (10–70%,
default 35%), disable it without deleting it, or remove it. Uploading enables the
image at the gateway. Each portrait controller must also enable **Settings →
Backdrop: gateway**; its colour palette and Light/Dark choice remain independent.

The gateway persists one normalized image and its settings in
`CONTROLLER_BACKDROP_STATE` (default `/data/backdrop.json`). Compose mounts the
`gateway_data` volume there; custom deployments must provide a writable persistent
`/data` mount for UID 10001. The Docker image creates that directory with the
correct ownership. To seed a local image, mount it read-only and set
`CONTROLLER_BACKDROP_FILE=/path/to/image.jpg`; it is imported only when no persisted
state exists. Subsequent uploads and removal take precedence over that seed.

`GET /device/backdrop` returns a fixed 480 × 800 RGB565 packet: `MCBG`, little-endian
16-bit width/height, then little-endian pixels (768008 bytes total). The image is
already darkened. ETag supports conditional refresh; 204 means disabled or absent.
The firmware polls every 15 seconds off the UI thread, caches in PSRAM for the
current boot, persists the opt-in in NVS, and retains the image during temporary
network failures. Disabling/removing at the gateway clears clients on their next
successful poll. A reboot needs one successful fetch; no image is written to flash.
Older gateways and failed downloads leave the plain palette available.

### Controller settings

`/settings` (public proxy path `/controller/settings`) lists connected compatible
physical controllers by stable device ID. Edit separate battery/external screen
timeouts in seconds, select Auto/Battery/USB mode, or reset to provisioned defaults.
Auto currently uses battery timing because reliable USB detection is unresolved.
Defaults are 30 seconds on battery and 0 (always on) externally. Values are saved
on each device, not in the gateway volume; offline devices reject changes.

The existing authenticated `/device/ws` accepts `controller/settings/state`
reports locally, without forwarding them to Music Assistant. Browser PUTs send a
correlated `controller/settings/set` event to the selected socket and await its
post-NVS-commit acknowledgement. A lost acknowledgement is explicitly unconfirmed;
reload and verify. Concurrent saves to the same controller are rejected. Devices
re-register after reconnect; gateway restarts require no preference recovery.

The example Caddyfile routes `/controller/settings*` to `/settings*`; add the same
LAN-only route to custom/Atlas reverse proxies. Writes require a same-host browser
Origin (including Tailscale hostnames), or the configured `CONTROLLER_ORIGIN`.
Reads/page access use the same trusted-LAN deployment model as the backdrop page.
