# Windows installer

New installation? Follow [Getting Started](../docs/getting-started.md) first to prepare Music Assistant, deploy the gateway, and configure trusted HTTPS. This installer flashes/configures the physical controller; it does not install backend services.

Download **music-controller-setup.exe** from the [latest release](https://github.com/lewiskingy/music-controller/releases/latest). Windows x64 is the initial supported platform. No Python, ESP-IDF, source checkout or firmware compilation is needed. Network access is required to fetch releases. The executable is currently unsigned, so Windows may show a SmartScreen prompt. Install the board's USB driver if no port appears.

Permanent download: https://github.com/lewiskingy/music-controller/releases/latest/download/music-controller-setup.exe

## Install or update

1. Connect the board using a USB data cable. Close serial monitors and other flashing tools.
2. Open the installer. It selects the newest compatible stable release and the first hardware profile. Confirm the release, hardware and USB port. Refresh to detect newly connected devices.
3. Choose **Update existing device** to preserve Wi-Fi, gateway key and preferences. The installer checks the installed partition table, writes the application, and resets OTA selection to boot the new application. It does not erase NVS or storage. A partition-layout mismatch is rejected; it is not silently converted into a destructive installation.
4. Choose **Set up new device** for a complete erase and installation. Enter Wi-Fi SSID/password, a certificate-trusted `wss://` gateway endpoint, and the gateway's existing device key. An optional Music Assistant player ID can be supplied; otherwise select a player on the controller.
5. Review the confirmation and keep USB connected until installation finishes. If automatic download mode fails, use the board's BOOT/RESET procedure, then retry.

New-device setup erases **all** flash, including existing settings and storage. Updates are USB operations, not wireless OTA. A failed/interrupted write may require another USB flash. USB identifies the chip/port but cannot identify the attached display: verify the hardware profile yourself.

Only releases with `release-manifest.json` appear in the picker. Older firmware-only/prototype releases and historical `gateway-v…` releases are not compatible. Release v0.5.0 supplies the executable and manifest; use a compatible stable release.

## Optional Wi-Fi sleep

New-device setup includes **Enable Wi-Fi sleep**, unchecked by default. Leave it
unchecked to keep Wi-Fi awake on battery and USB, including when the screen is
off. Checking it saves power by enabling modem sleep, which should retain the
connection but may add latency. The CLI provisioning flow asks the same question
and defaults to No.

Setup writes `controller/wifi_sleep` as NVS `u8`: `0` disables sleep and `1`
enables it. Firmware reads this at boot. This option requires firmware containing
PR #35; older firmware ignores it. The checkbox applies only to **Set up new
device**, including prepare-only packages. **Update existing device** preserves
the stored setting and ignores the setup fields. Changing this option through
new-device setup erases all device data, so do not reprovision just to obtain
the new awake default: an ordinary firmware update uses it when the key is absent.

## Gateway key

For an existing gateway, reuse its `CONTROLLER_DEVICE_TOKEN`; normal firmware updates leave it unchanged. For a new gateway, select **Generate new gateway key**, then copy it or explicitly export the matching environment setting. The key is shared by the current gateway implementation: changing it requires reprovisioning other devices using that gateway.

The exported `.env` fragment contains only `CONTROLLER_DEVICE_TOKEN`. Add it to your gateway's existing environment configuration and redeploy. It does not contain Music Assistant credentials and does not configure a remote container automatically. Existing files are never overwritten by export. Do not commit or share keys, exported configuration, or NVS binaries.

## Temporary files and prepare-only

Downloads, intermediate settings CSV, and generated NVS are removed when a normal operation finishes or fails. An abrupt process termination/power loss may leave temporary files behind. Cleanup is ordinary file deletion, not secure disk erasure.

**Prepare files only** asks for a parent folder and creates a new release/profile folder with verified binaries, selected manifest and, for new-device setup, private NVS. Nothing is flashed. Keep that folder private; credentials are inside NVS. Use a normal installation when ready to flash; prepare-only is useful for inspection/manual flashing and does not provide an offline installer mode.

## Command line

Download `music-controller-cli.exe` from the same release for console progress and command-line automation. The GUI executable is built as a Windows windowed application; the CLI is a separate entry point to the same implementation.

```powershell
.\music-controller-cli.exe releases
.\music-controller-cli.exe install --rel v0.5.0 --port COM4
.\music-controller-cli.exe provision --rel v0.5.0 --port COM4 --export-gateway controller-gateway.env
.\music-controller-cli.exe provision --rel v0.5.0 --prepare-only --keep-files .\private-controller-package
```

Omit `--rel` for the latest compatible stable release. Omit `--port` only when exactly one serial port is present. Flashing requires typing `FLASH` after reviewing the target. Passwords and tokens are collected interactively, never through command-line arguments. `--keep-files DIRECTORY` also retains files during an installation; the directory must be new. Without retention, temporary files are cleaned up. `--prepare-only` requires `--keep-files` in CLI mode.

`--profile waveshare-esp32s3-touch-lcd-4.3` explicitly chooses the current board. One profile is selected automatically; future multi-profile releases require a choice in CLI mode. Resolution is descriptive metadata today, not an arbitrary NVS override. Adding another board requires its firmware build/profile and, if necessary, an installer update for its chip, partition layout or provisioning schema.

## Releases and Atlas

A single `vX.Y.Z` tag versions firmware, installer, gateway source and prototype. Successful main builds now prepare tagged drafts automatically; a maintainer reviews/tests and publishes them manually. [Release operations and asset guide](../docs/releases.md). The release workflow builds firmware using ESP-IDF 5.4.2, creates checksummed per-profile flash assets from `flasher_args.json`, builds both Windows executables, runs installer/gateway tests, builds the gateway container, and attaches assets to a draft only after all component jobs succeed. Drafts and prereleases are excluded from the stable installer picker.

The gateway is still built from source by deployment consumers; this does **not** introduce a container registry requirement. A gateway source archive is attached to the release and the manifest records its matching main tag. Atlas should pin its external gateway build to that same `vX.Y.Z` tag instead of `gateway-vX.Y.Z`. This change does not update Atlas's pin or deploy anything. Historical tags remain valid. Describe which components changed in release notes; a firmware-only change does not itself require deploying an unchanged gateway.

Downloads use HTTPS with normal certificate verification, bounded responses, and SHA-256/size checks against the selected release manifest. SHA-256 verifies payload integrity against the manifest; it is not an independent signature or publisher identity check. GitHub account/release security remains the trust boundary.

## Development

```sh
python -m pip install -r installer/requirements.txt pytest
python installer/setup.py
python installer/setup.py releases
python -m pytest installer/tests -q
python installer/setup.py --self-test
```

On Windows, install `pyinstaller==6.16.0` and run the packaging commands in `.github/workflows/installer.yml`. That workflow uploads the executables for PR review before tagging. The frozen CLI self-test imports the bundled tools and generates a real NVS partition without network or hardware. Physical acceptance still requires a Windows PC and board: full provisioning, update preserving settings, release/port selection, BOOT recovery, gateway authentication and cleanup after a failed write.


## Serial Monitor (Windows GUI)

The expandable **Serial Monitor · Diagnostics** section reads the selected COM port at
115200 baud. Select **Show logs**, then **Start monitoring** to follow firmware boot,
Wi-Fi and gateway messages without separate terminal software. You can copy,
clear or save the bounded log (last 1,500 lines). The monitor retries automatically
if the USB serial port temporarily disappears. The monitor opens the port without
asserting DTR/RTS to minimise unintended controller resets.

When flashing starts, the monitor stops and releases the COM port before esptool
connects; after flashing (including failures) it resumes if previously active.
The monitor never opens the port automatically merely by starting the installer.
Close any *external* serial-monitor software before flashing: Windows cannot
share an active COM port between unrelated processes.

Saved logs can include network identifiers, gateway details or other diagnostics.
Review and redact sensitive information before sharing them.
