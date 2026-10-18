# Getting started: hardware to first playback

This guide installs a physical Music Controller with your own Music Assistant and gateway. For supported parts and optional prints, start with the [hardware guide](hardware.md). Allow time for server/network configuration as well as flashing; the Windows installer only configures the device.

## 1. Prepare Music Assistant

1. Install Music Assistant using its [official installation guide](https://www.music-assistant.io/installation/), or use your existing installation.
2. Configure your music providers and an output player. From Music Assistant itself, select music and confirm that player actually produces audio.
3. In Music Assistant's user/account settings, create a dedicated **non-admin** controller account. Keep its username and password on the gateway host.
4. Note the Music Assistant WebSocket address, typically `http://YOUR_MA_HOST:8095/ws`. Use an address reachable from the gateway container, not `localhost` inside a different container.

Music Assistant owns the music catalogue, provider subscriptions, queues and audio delivery. Installing this project does not install or configure those services.

## 2. Deploy the gateway

Use an always-running host reachable from the controller's Wi-Fi. The host can be the same machine as Music Assistant. Install Docker Engine and Compose v2 using [Docker's installation instructions](https://docs.docker.com/engine/install/).

Download a release source checkout or clone this repository and select the stable release you intend to install. New releases use one `vX.Y.Z` tag for firmware and gateway source; check release notes when updating existing installations.

```sh
git clone https://github.com/lewiskingy/music-controller.git
cd music-controller
git checkout v0.5.0  # Example: replace with your selected stable release.
cd gateway
cp .env.example .env
chmod 600 .env
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
```

Edit `.env`, replacing placeholders:

| Setting | Example / purpose |
| --- | --- |
| `MA_WS_URL` | `http://192.168.1.20:8095/ws` — Music Assistant reachable from the container |
| `MA_USERNAME`, `MA_PASSWORD` | Dedicated Music Assistant account from step 1 |
| `CONTROLLER_DEVICE_TOKEN` | The random value generated above; save it securely for the installer |
| `CONTROLLER_HOST` | `controller.example.net` — used by the optional Caddy frontend |
| `CONTROLLER_BIND_IP` | Your server's LAN IP for the optional frontend; default `127.0.0.1` cannot serve a physical controller |

Replace example names and addresses with yours. `host.docker.internal` in the template means the Docker host; it is not a discovery name for Music Assistant on another machine.

```sh
docker compose config --quiet
docker compose up -d --build gateway
curl --fail http://127.0.0.1:8080/health
docker compose logs --tail 50 gateway
```

The raw gateway listens on host loopback port 8080 for a reverse proxy. A successful health response proves configuration is present, **not** a successful Music Assistant login; verify that when the device connects. Keep the named `gateway_data` volume for provider-logo overrides.

See [gateway deployment](../gateway/README.md) for Python operation, proxy routing and detailed validation.

## 3. Configure DNS and trusted HTTPS

The device requires `wss://` with certificate verification. You need a hostname that resolves from its Wi-Fi to your frontend, and a certificate issued by a CA trusted by the firmware. An IP address with a self-signed certificate or Caddy's local CA is not a working substitute.

### Existing trusted reverse proxy

For a host proxy on the gateway machine, this Caddy example uses an **existing publicly trusted certificate and matching private key**. Replace the hostname and file paths; make the files readable only as required by your proxy. Configure certificate renewal in your existing certificate-management system.

```caddyfile
controller.example.net {
    tls /etc/caddy/certs/controller-fullchain.pem /etc/caddy/certs/controller-key.pem
    handle /controller/device/ws {
        rewrite * /device/ws
        reverse_proxy 127.0.0.1:8080
    }
    handle /controller/device/artwork/* {
        uri strip_prefix /controller
        reverse_proxy 127.0.0.1:8080
    }
    handle /controller/settings* {
        uri strip_prefix /controller
        reverse_proxy 127.0.0.1:8080
    }
}
```

Configure LAN DNS so `controller.example.net` resolves to the proxy's LAN IP. Permit access from the device's Wi-Fi to TCP 443, and restrict ingress to your trusted network. A proxy in another container must share a Docker network and use `gateway:8080` instead of its own loopback. The [bundled Caddyfile](../gateway/Caddyfile) proxies device and settings routes; it no longer serves a browser client.

### New frontend without an existing certificate

Obtain a publicly trusted certificate for a domain you control using DNS validation, for example [Caddy's DNS challenge](https://caddyserver.com/docs/automatic-https#dns-challenge). This requires a Caddy build with your DNS provider's module and that provider's credentials; the bundled stock `caddy:2` image does not supply provider modules. Use the resulting DNS-capable proxy or supply the certificate to the existing-proxy example above. Keep DNS API credentials on the server.

The optional Compose frontend can use an already configured certificate-issuance environment:

```sh
docker compose --profile web config --quiet
docker compose --profile web up -d --build
```

It requires the hostname/origin/LAN bind address above, ports 80/443 free on that address, and successful trusted certificate issuance. **It is not a complete DNS-challenge setup.** Do not enable public router forwarding just to make the controller work. A LAN server can use DNS validation without exposing the controller publicly.

Before flashing, open `https://YOUR_HOST/controller/settings` from a computer on the same network. It should load without certificate warnings (no connected controllers yet is normal). This checks browser connectivity; the physical first-boot test below also checks the ESP32's trust bundle.

## Credentials: what goes where?

| Credential | Gateway host | Physical controller |
| --- | --- | --- |
| Music Assistant username/password | `MA_USERNAME` / `MA_PASSWORD` | Never |
| Controller key | `CONTROLLER_DEVICE_TOKEN` | Installer's gateway/device key; must match exactly |
| Wi-Fi SSID/password | Not required by the gateway | Entered in installer, saved in private NVS |
| TLS private key / DNS API credentials | Frontend/certificate-management service | Never |

The controller key is separate from the Music Assistant account and its password. The current gateway shares one controller key across devices; rotating it requires reprovisioning all affected devices. The installer can also generate a key, but you must apply that same value to the gateway and restart it yourself.

## 4. Install and configure the controller

On Windows x64, download [music-controller-setup.exe](https://github.com/lewiskingy/music-controller/releases/latest/download/music-controller-setup.exe). Python, ESP-IDF and compiling firmware are not required. The executable is unsigned; Windows may show SmartScreen.

1. Connect the supported board using a USB **data** cable. Close serial monitors.
2. Open the installer; confirm the release, **Waveshare ESP32-S3 Touch LCD 4.3** profile and correct COM port. The profile's 800×480 describes the physical panel; the UI rotates to portrait 480×800.
3. Choose **Set up new device**. This erases the board's existing firmware/settings.
4. Enter Wi-Fi SSID/password, `wss://YOUR_HOST/controller/device/ws`, and the matching `CONTROLLER_DEVICE_TOKEN`. An initial Music Assistant player ID is optional; you can select a player on screen.
5. Review and flash. Keep USB connected until completion, then let the board restart.

For an already configured controller choose **Update existing device** to preserve Wi-Fi, keys and preferences. Do not choose a full erase for an ordinary update. See the [installer guide](../installer/README.md) for BOOT recovery, CLI and prepare-only operation; see [manual firmware installation](../firmware/esp32/README.md) for non-Windows systems or installer fallback.

## 5. Verify first playback

- The Loading image gives way to the portrait controller UI.
- Wi-Fi and the gateway connect; players and real track metadata appear.
- Select the intended player and test playback, Stop and volume on that player.
- Browse/Search your configured provider, select an item and check Queue/artwork.
- Change playback in Music Assistant and confirm that the controller follows it.
- Restart the device: player/provider preferences should persist. Restart the gateway and confirm reconnection.

A healthy container without player data is not a completed installation. Use [troubleshooting](troubleshooting.md) for DNS, TLS, key, Music Assistant login or blank-screen failures.

## 6. Preferences, battery and case

Use the device's Settings → Screen power to choose timeouts. Auto currently uses battery timing even on USB: manually select USB/external for that timeout. Defaults are 30 seconds on battery and always on externally. The first touch wakes the backlight without activating the control underneath. The boot/pre-sleep image is bundled firmware artwork, not an uploaded permanent background.

`https://YOUR_HOST/controller/settings` adjusts connected devices. Provider-logo overrides are at `/controller/settings/providers`; preserve the gateway data volume for those uploads. Settings pages use trusted-LAN access, not separate user login.

Once USB operation works, follow the [case assembly guide](../hardware/enclosure/ASSEMBLY.md). A battery and contact-charging dock are optional; check battery polarity and dimensions before connecting it. No battery runtime or automatic power-source detection is promised.

## 7. Later updates

Use the installer update mode for firmware. For the gateway, check out the chosen release on its host and run `docker compose up -d --build gateway` from `gateway/`; retain `.env` and the data volume. Review release notes for compatibility and deployment order. New releases share a `vX.Y.Z` source tag; historical `gateway-v…` tags remain valid for existing deployments. A firmware-only change does not require redeploying an unchanged gateway.

For where to find installer/firmware/gateway files and how maintainers promote tested drafts, see [release preparation and downloads](releases.md).
