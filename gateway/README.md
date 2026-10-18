# Music Controller gateway

The companion backend for ESP32 firmware and the browser prototype. It uses Python 3.12/aiohttp to authenticate to your Music Assistant server and relay a limited set of requests and live events. It does not serve audio or own a music library.

For the complete installation sequence, follow [Getting Started](../docs/getting-started.md). This repository owns the gateway; Atlas is one independent deployment consumer, not a prerequisite. Current firmware uses the gateway for playback, Queue, Browse, Search, artwork, provider icons and settings.

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

See [DNS and trusted HTTPS setup](../docs/getting-started.md#3-configure-dns-and-trusted-https) for a host-proxy example using existing trusted certificate files, and DNS-challenge requirements for a new certificate. A physical device needs this before installation can succeed.

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
| /controller/settings* | Gateway `/settings*` (device settings and provider-logo uploads) |

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

## Device capabilities

After `gateway/ready`, the gateway emits `gateway/capabilities` advertising `seek`, `queue_item_play`, `browse`, `search` and `artwork_rgb565`. Current portrait firmware uses these capabilities to enable its controls. Older firmware ignores unsupported capabilities. Keep firmware/gateway versions compatible and review release notes for deployment order; existing URLs and keys do not need changing for ordinary updates.

- Seek accepts integer seconds through `player_queues/seek`.
- Queue playback accepts stable item IDs as well as legacy numeric positions.
- `controller/browse` translates fixed catalogue/detail/folder requests into compact pages of at most 20 rows.
- `controller/search` accepts one provider and one media type, normalizes query case, and returns compact rows. An explicit unsupported `providers` argument permits one retry within the same provider's indexed library, labeled `scope: library`; it never switches to all-provider search.
- Current queue items receive opaque `controller_art_id` values. Devices fetch fixed 288×288 RGB565 packets through `/device/artwork/{art_id}` with the same token. Missing/failing images get a generic cover; requests cannot supply arbitrary image URLs. Pillow is pinned in `requirements.txt` for bounded image conversion.

The [protocol contract](../contracts/music-assistant.md) gives bounds, packet format and routing. Automated tests use simulated Music Assistant responses; validate your actual providers and device after deployment. If you use an existing reverse proxy rather than this example, add the artwork route as well as the device WebSocket route.

### Device splash image

Boot and pre-sleep images are bundled with the firmware. The optional uploaded
background feature and its routes have been removed; normal screens use their
selected colour palette. Existing `CONTROLLER_BACKDROP_STATE` and
`CONTROLLER_BACKDROP_FILE` configuration is ignored, and old saved image files
are no longer read. The persistent `/data` mount remains needed for provider
logo overrides. Consumers can remove obsolete `/controller/backdrop*` and
`/controller/device/backdrop` proxy rules on their next deployment. The firmware
splash works with existing gateways and requires no release-pin change.

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
Reads/page access use the trusted-LAN deployment model.

### Provider logos

The ESP32 header shows the selected browsing/search provider beside the player.
The same full-colour icon is used on the selected provider row; other rows show
grey icons and muted names. Selection is still exclusive and uses the existing
explicit Save behavior. The Now Playing artwork badge identifies the actual
stream provider independently of the selected browsing provider. Ambiguous or
unknown sources have no badge: a multi-provider library item is not arbitrarily
labelled with the first mapping or selected provider.

The gateway discovers provider instances from authenticated MA `providers`
responses, assigns opaque `controller_icon_id` values, and requests colour icons
through MA `providers/icon` (`provider`, `variant: default`). Older servers can
supply inline `icon_svg` through `providers/manifests`. Unsupported APIs, missing
or invalid logos fall back to a generic icon without breaking playback.

Open `/controller/settings/providers` (raw gateway `/settings/providers`) to
preview discovered providers and upload a **per-instance** SVG, PNG, JPEG or WebP
override, or remove it to return to MA's icon. This is useful for Navidrome:
Music Assistant's provider can be OpenSubsonic and therefore have a generic
Subsonic logo rather than a Navidrome logo. We do not assume that every
OpenSubsonic server is Navidrome. Multiple accounts/servers can have different
uploaded icons. No provider names or URLs need to be compiled into firmware.

Overrides are normalized to transparent 40×40 pixels and atomically stored in
`CONTROLLER_PROVIDER_ICONS_STATE` (default `/data/provider-icons.json`) on the
existing persistent gateway volume. At most 64 overrides are supported. Uploads
are limited to 256 KiB/one million decoded pixels; SVG external resources and
embedded content are rejected. Original SVG/HTML is never served to browsers:
previews are normalized PNG and native devices receive RGB565+A8 packets.
The upload page uses the existing trusted-LAN/Tailscale settings route and
same-host Origin checks. The existing Tailscale Serve mount also supports it.

Device downloads use the existing authenticated artwork route:
`/controller/device/artwork/provider-{controller_icon_id}`. Firmware refreshes
icons approximately every 30 seconds in a worker and keeps a bounded PSRAM cache;
transient failures retain the last valid icon. Existing Atlas settings/artwork
routes are sufficient. Deploy the updated gateway and firmware; no NVS
reprovisioning or additional proxy paths are required for this feature. A gateway
release/pin update is still needed for Atlas deployment.

## Unified release version

New releases use the repository's main `vX.Y.Z` tag for both firmware and gateway.
Deployments that build this gateway from a pinned checkout (including Atlas)
should use that tag; separate `gateway-v…` tags remain historical. The release
includes a gateway source archive, not a registry-published image. The Windows
[installer](../installer/README.md) can reuse or generate the matching
`CONTROLLER_DEVICE_TOKEN` and explicitly export its environment setting. Apply
that setting to the container yourself; no Music Assistant credentials go onto
the device. Normal firmware updates preserve the device key.
