#!/usr/bin/env python3
"""Create a private ESP32 NVS image. Run locally, never in public CI."""
import csv
import getpass
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
IDF_PATH = os.environ.get("IDF_PATH")
if not IDF_PATH:
    sys.exit("Activate ESP-IDF first (IDF_PATH must be set).")
generator = Path(IDF_PATH) / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
if not generator.is_file():
    sys.exit(f"Missing ESP-IDF NVS generator: {generator}")

print("Local provisioning only. Do not commit the output or share passwords.")
ssid = input("Wi-Fi SSID: ").strip()
password = getpass.getpass("Wi-Fi password: ")
gateway = input("Gateway WSS URL [wss://music.theflat.me.uk/controller/device/ws]: ").strip() or "wss://music.theflat.me.uk/controller/device/ws"
token = getpass.getpass("Device token (same as Atlas CONTROLLER_DEVICE_TOKEN): ").strip()
player = input("Optional Music Assistant player ID [auto-select]: ").strip()
if not ssid or not token or not gateway.startswith("wss://"):
    sys.exit("SSID, device token and secure wss:// URL are required.")
if len(ssid.encode()) > 32 or len(password.encode()) > 64 or len(token.encode()) > 128:
    sys.exit("One or more configuration values are too long.")

output = ROOT / "controller-nvs.bin"
with tempfile.TemporaryDirectory() as temp:
    csv_path = Path(temp) / "config.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["controller", "namespace", "", ""])
        for key, value in [
            ("wifi_ssid", ssid), ("wifi_pass", password),
            ("gateway_url", gateway), ("device_token", token),
            ("player_id", player)
        ]:
            if value:
                writer.writerow([key, "data", "string", value])
    subprocess.run([sys.executable, str(generator), "generate", str(csv_path), str(output), "0x6000"], check=True)
print(f"Private image created: {output}")
print("Flash only this NVS partition at 0x9000 AFTER flashing the full firmware image.")
print("Keep the image private; it contains Wi-Fi and device credentials.")
