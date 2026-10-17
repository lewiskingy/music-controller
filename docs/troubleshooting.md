# Troubleshooting

Start with the [first-playback checklist](getting-started.md#5-verify-first-playback). Work from hardware through Wi-Fi, TLS, gateway authentication and Music Assistant; a running container alone does not prove the connection chain.

| Symptom | Check / next action |
| --- | --- |
| No USB port in installer | Use a data-capable cable, check Device Manager/drivers, try another port and refresh. Confirm the exact board model. |
| Cannot connect for flashing | Close serial monitors. Follow the board's BOOT/RESET download-mode procedure and retry the correct port. |
| Installer update rejects partition layout | Do not force-write an application against an unknown layout. Preserve your settings securely and use deliberate new-device setup only when prepared to erase/reprovision. |
| File not found in manual flashing | Extract the ZIP and check the working directory/file path. Actions `music_controller.bin` and tagged release `waveshare-4.3-application.bin` are different package filenames. Neither is a merged image for address `0x0`. |
| Blank screen / resets before networking | Confirm supported hardware and correct flash package. Capture serial startup logs; inspect reset reason/display initialization. Do not repeatedly erase NVS to fix application/display failures. |
| Wi-Fi will not connect | Check SSID/password, signal and access to a 2.4 GHz Wi-Fi network. Reprovision only if settings need changing. |
| TLS / WSS connection failure | Verify the hostname resolves from the device network, LAN TCP 443 is reachable, certificate hostname/full chain are correct and trusted, and the device can establish correct time. A browser accepting a local CA does not prove firmware trusts it. Keep verification enabled. |
| HTTP 403 before WebSocket upgrade | Device key must exactly match gateway `CONTROLLER_DEVICE_TOKEN`; proxy must preserve `X-Device-Token`. For browser access check exact `CONTROLLER_ORIGIN`. |
| `gateway/error`, no players or repeated disconnect | Check gateway logs, MA account credentials and reachability of `MA_WS_URL` from the container. `localhost` there means the gateway container. |
| Health passes but device cannot play | `/health` checks configuration presence, not MA login. Connect the device and inspect gateway login/command results. Test playback directly in MA. |
| Metadata works but artwork does not | Route `/controller/device/artwork/*` to `/device/artwork/*`, preserve the device header, and check gateway/provider logs. Missing artwork can use a placeholder. |
| Settings page unavailable | Route `/controller/settings*` to `/settings*` and preserve browser Origin. Devices must be online for changes; reload to verify an unconfirmed save. |
| Display turns off while on USB | Auto uses the battery timeout. Select USB/external manually; 0 seconds means always on. First touch wakes without activating a hidden control. |
| Sonos Pause unavailable | Deliberately disabled while playing because of observed queue hopping. Use Stop. |
| No sound | The controller does not output audio. Verify the selected player and playback directly in Music Assistant; its streaming/player network path is separate. |

## Gather useful diagnostics

On the gateway host, from `gateway/`:

```sh
docker compose logs --tail 100 gateway
curl --fail http://127.0.0.1:8080/health
```

For device serial logs, close the installer first:

```powershell
py -m pip install pyserial
py -m serial.tools.miniterm COM5 115200
```

Replace COM5 with your port. Capture a cold boot and the failing action, including reset reason, Wi-Fi/DHCP, WebSocket/gateway state and heartbeat messages. See [firmware history](firmware-history.md) for past display/memory investigations; use the current installer/firmware guide for recovery instructions.

Report release/commit, board model, Music Assistant version, reproduction steps and redacted logs through [contributor guidance](../CONTRIBUTING.md). Never attach `.env`, exported key files or private NVS binaries.
