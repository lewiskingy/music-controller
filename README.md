# Music Controller

A dedicated household touchscreen remote for [Music Assistant](https://www.music-assistant.io/). Choose a player, browse or search music, and control playback without opening a phone app.

The controller runs on a supported ESP32 touchscreen. It controls your existing speakers/players through Music Assistant; audio travels from Music Assistant to those players. The controller and gateway do not play or relay audio.

**[Start here: hardware to first playback →](docs/getting-started.md)** · [Download Windows installer](https://github.com/lewiskingy/music-controller/releases/latest/download/music-controller-setup.exe) · [Releases](https://github.com/lewiskingy/music-controller/releases)

## What is included?

| Component | What you get |
| --- | --- |
| [ESP32 firmware](firmware/esp32/README.md) | Portrait 480×800 touch UI: Now Playing, player/provider selection, artwork, Queue, Browse, Search, playback/seek/volume controls, colour preferences and backlight power saving |
| [Dedicated gateway](gateway/README.md) | Required service connecting the controller to Music Assistant, including authentication, artwork conversion, device settings and provider-logo overrides |
| [Windows installer](installer/README.md) | Prebuilt firmware installation and initial Wi-Fi/gateway configuration; updates preserve settings |
| [Case and dock designs](hardware/enclosure/README.md) | Printable STL/STEP designs, renders and assembly instructions; first-fit prototype, not yet physically validated |
| [Browser prototype](docs/prototype.md) | Optional older landscape interaction prototype; not a preview of every current firmware feature |

## What do I need?

| Requirement | Details |
| --- | --- |
| Supported touchscreen | **Original Waveshare ESP32-S3-Touch-LCD-4.3**, 800×480 capacitive GT911, 16 MB flash / 8 MB PSRAM. Current firmware supports this board only; 4.3B/C, industrial and 4-inch models are not interchangeable. [Buying guide](docs/hardware.md) |
| Power and flashing | USB data cable, suitable USB power supply, and a Windows x64 computer for the packaged installer. Manual flashing/build instructions are available for other systems. |
| Battery | **Optional** for portable operation; USB-powered operation needs no battery. Use a compatible protected single-cell 3.7 V LiPo with verified polarity and charging suitability. [Battery and case requirements](docs/hardware.md#battery-and-enclosure) |
| Music Assistant | A working installation on your network, configured music providers, at least one working output player, and a dedicated non-admin account for the gateway |
| Gateway host | An always-running network host with Docker Compose v2, able to reach Music Assistant; it may share the Music Assistant host |
| Network frontend | Wi-Fi access to the gateway through trusted HTTPS/WSS, with working DNS and a certificate trusted by the firmware. [Network setup](docs/getting-started.md#3-configure-dns-and-trusted-https) |

You do not need Atlas, this author's domain, a Home Assistant installation specifically, or ESP-IDF to use the Windows installer. You do need to deploy the gateway yourself; the installer does not install backend services.

## Setup overview

1. [Choose hardware](docs/hardware.md); start with USB power before assembling a case or connecting a battery.
2. [Prepare Music Assistant](docs/getting-started.md#1-prepare-music-assistant) and verify playback on your chosen speaker/player.
3. [Deploy the gateway](docs/getting-started.md#2-deploy-the-gateway), configure its Music Assistant account and generate a separate controller key.
4. [Configure trusted HTTPS/WSS](docs/getting-started.md#3-configure-dns-and-trusted-https) reachable from the controller's Wi-Fi.
5. [Install firmware](docs/getting-started.md#4-install-and-configure-the-controller) with Wi-Fi settings, your gateway URL and the matching controller key.
6. [Verify first playback](docs/getting-started.md#5-verify-first-playback), set preferences, then optionally assemble the case.

## Current limits

- Automatic USB/battery power detection is unresolved. Auto uses the battery timeout; select USB/external manually for a permanently powered controller.
- Power saving switches off the backlight, not the whole device. Battery runtime has not been established.
- Sonos Pause is disabled while playing because of observed queue hopping; use Stop.
- Updates use USB; wireless OTA delivery is not implemented.
- No specific Music Assistant version is currently certified; verify the installed server and providers using the setup checklist.
- The enclosure and optional contact-charging dock require physical fit/electrical validation. USB-only assembly is the simplest starting point.

## Documentation

[Getting started](docs/getting-started.md) · [Hardware](docs/hardware.md) · [Gateway deployment](gateway/README.md) · [Firmware/manual installation](firmware/esp32/README.md) · [Windows installer](installer/README.md) · [Case assembly](hardware/enclosure/ASSEMBLY.md) · [Troubleshooting](docs/troubleshooting.md)

For implementation details: [architecture](docs/architecture.md), [protocol](contracts/music-assistant.md), [development and releases](docs/development.md), and [firmware engineering history](docs/firmware-history.md).

Keep `.env`, controller keys and private NVS binaries private. Deploy on a trusted LAN; the example browser/settings frontend is not a public authenticated service.

## Contributing

Bug reports, improvements and hardware testing are welcome. Read **[CONTRIBUTING.md](CONTRIBUTING.md)** before starting.

**Using an AI coding agent? It must read [AGENTS.md — required repository instructions](AGENTS.md) before making changes, plus any AGENTS.md instructions in the directories it modifies.**
