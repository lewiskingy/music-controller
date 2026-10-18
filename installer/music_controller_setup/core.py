"""Release discovery, verified preparation, provisioning and USB flashing."""
import csv
import contextlib
import io
import hashlib
import json
from pathlib import Path
import re
import secrets
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, urlopen

REPOSITORY = "lewiskingy/music-controller"
API = f"https://api.github.com/repos/{REPOSITORY}"
MANIFEST = "release-manifest.json"
MAX_DOWNLOAD = 32 * 1024 * 1024


class SetupError(ValueError):
    """An actionable error safe to show without exposing provisioning values."""


def download(url, limit=MAX_DOWNLOAD):
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in {
        "api.github.com", "github.com", "release-assets.githubusercontent.com",
        "objects.githubusercontent.com",
    }:
        raise SetupError("Release downloads must use GitHub HTTPS URLs")
    with urlopen(Request(url, headers={"User-Agent": "MusicControllerSetup"}), timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise SetupError("Release download exceeds size limit")
    return data


def releases():
    """Only releases containing an installer manifest are installable."""
    result = []
    for page in range(1, 11):
        batch = json.loads(download(f"{API}/releases?per_page=100&page={page}", 4 * 1024 * 1024))
        result.extend(r for r in batch if not r["draft"] and any(
            a["name"] == MANIFEST for a in r["assets"]))
        if len(batch) < 100:
            break
    return result


def release(tag=None):
    if tag:
        if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", tag):
            raise SetupError("Expected a release tag such as v0.5.0")
        return json.loads(download(f"{API}/releases/tags/{tag}"))
    return next((r for r in releases() if not r["prerelease"]), None) or _no_release()


def _no_release():
    raise SetupError("No installer-compatible stable release is available yet")


def manifest(selected):
    assets = {a["name"]: a for a in selected["assets"]}
    if MANIFEST not in assets:
        raise SetupError("This release predates the installer; select a newer release")
    data = json.loads(download(assets[MANIFEST]["browser_download_url"], 1024 * 1024))
    if data.get("schema_version") != 1 or data.get("version") != selected["tag_name"]:
        raise SetupError("Unsupported or mismatched release manifest")
    if not data.get("profiles"):
        raise SetupError("Release has no hardware profiles")
    return data


def profile_for(data, profile_id=None):
    profiles = data["profiles"]
    if profile_id is None and len(profiles) == 1:
        return profiles[0]
    for profile in profiles:
        if profile["id"] == profile_id:
            return profile
    raise SetupError("Select a hardware profile: " + ", ".join(p["id"] for p in profiles))


def validate_profile(profile):
    if profile["chip"] != "esp32s3" or profile["provisioning_schema"] != "controller-v1":
        raise SetupError("This installer does not support the selected chip/settings schema")
    nvs = profile["nvs"]
    if nvs != {"offset": 0x9000, "size": 0x6000}:
        raise SetupError("Unsupported NVS layout; use an updated installer")
    # Updates preserve settings only for this known partition layout.
    expected = {"bootloader": 0, "partitions": 0x8000, "otadata": 0xf000, "application": 0x20000}
    if len(profile["files"]) != 4 or len({f["asset"] for f in profile["files"]}) != 4:
        raise SetupError("Release must contain four unique flash inputs")
    if {f["role"]: f["offset"] for f in profile["files"]} != expected:
        raise SetupError("Unsupported partition layout; use an updated installer")
    for f in profile["files"]:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+\.bin", f["asset"]) or not re.fullmatch(r"[a-f0-9]{64}", f["sha256"]):
            raise SetupError("Invalid release file metadata")
        limits = {"bootloader": 0x8000, "partitions": 0x1000, "otadata": 0x2000, "application": 0x500000}
        if not 0 < f["size"] <= limits[f["role"]]:
            raise SetupError("Invalid release file size")


def prepare(selected, profile, destination, log=print):
    validate_profile(profile)
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    assets = {a["name"]: a for a in selected["assets"]}
    for f in profile["files"]:
        log("Downloading " + f["asset"])
        data = download(assets[f["asset"]]["browser_download_url"], f["size"])
        if len(data) != f["size"] or hashlib.sha256(data).hexdigest() != f["sha256"]:
            raise SetupError("Release checksum/size mismatch: " + f["asset"])
        (destination / f["asset"]).write_bytes(data)
    (destination / MANIFEST).write_text(json.dumps({"version": selected["tag_name"], "profile": profile}, indent=2))


def validate_settings(settings):
    if type(settings.get("wifi_sleep", False)) is not bool:
        raise SetupError("Wi-Fi sleep must be enabled or disabled")
    limits = {"wifi_ssid": 32, "wifi_pass": 64, "gateway_url": 191, "device_token": 128, "player_id": 128}
    for key, limit in limits.items():
        value = settings.get(key, "")
        if "\0" in value or len(value.encode("utf-8")) > limit:
            raise SetupError(f"Invalid length or NUL character in {key}")
    url = urlparse(settings.get("gateway_url", ""))
    if not settings.get("wifi_ssid") or not settings.get("device_token") or url.scheme != "wss" or not url.hostname or url.username or url.password:
        raise SetupError("Wi-Fi SSID, gateway key and a secure wss:// gateway URL are required")


def new_key():
    return secrets.token_urlsafe(32)


def provision(settings, profile, destination):
    from esp_idf_nvs_partition_gen import nvs_partition_gen
    validate_profile(profile)
    validate_settings(settings)
    destination = Path(destination)
    # CSV is removed even when NVS generation fails; never log settings.
    with tempfile.TemporaryDirectory(prefix="controller-nvs-") as temp:
        source = Path(temp) / "settings.csv"
        with source.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["key", "type", "encoding", "value"])
            writer.writerow(["controller", "namespace", "", ""])
            writer.writerow(["wifi_sleep", "data", "u8", int(settings.get("wifi_sleep", False))])
            for key in ("wifi_ssid", "wifi_pass", "gateway_url", "device_token", "player_id"):
                if settings.get(key):
                    writer.writerow([key, "data", "string", settings[key]])
        from argparse import Namespace
        try:
            nvs_partition_gen.generate(Namespace(input=str(source), output="controller-nvs.bin", size=hex(profile["nvs"]["size"]), version=2, outdir=str(destination.resolve())))
        except SystemExit:
            raise SetupError("Could not generate settings; check configuration and available disk space") from None
    (destination / "controller-nvs.bin").chmod(0o600)


def export_gateway_key(token, destination):
    """Export explicitly only; quote existing keys safely for Compose env files."""
    if any(c in token for c in "\r\n\0"):
        raise SetupError("Gateway key cannot contain line breaks")
    value = token.replace("\\", "\\\\").replace("'", "\\'")
    path = Path(destination)
    with path.open("x", encoding="utf-8") as stream:
        stream.write(f"CONTROLLER_DEVICE_TOKEN='{value}'\n")
    path.chmod(0o600)


def ports():
    from serial.tools.list_ports import comports
    return [(p.device, p.description) for p in comports()]


def flash(profile, destination, port, full=False, log=None):
    import esptool
    validate_profile(profile)
    destination = Path(destination)
    files = profile["files"] if full else [f for f in profile["files"] if f["role"] in ("otadata", "application")]
    args = ["--chip", profile["chip"], "--port", port, "--baud", "460800"]
    def run(arguments, stage):
        # PyInstaller --windowed otherwise swallows esptool diagnostics because
        # it has no console. Capture only the USB stage: never provisioning data.
        if log:
            log(stage + "…")
        capture = io.StringIO()
        try:
            with contextlib.redirect_stdout(capture), contextlib.redirect_stderr(capture):
                esptool.main(arguments)
        except (SystemExit, esptool.FatalError, OSError) as error:
            lines = [line.strip() for line in capture.getvalue().splitlines()
                     if line.strip() and not line.startswith("esptool.py v")]
            # Keep actionable esptool diagnostics but avoid dumping arbitrary
            # serial/device output, paths or private provisioning values.
            detail = next((line for line in reversed(lines)
                           if any(word in line.lower() for word in
                                  ("fatal error", "failed to", "timed out", "serial", "port",
                                   "invalid head", "unexpected packet", "wrong boot",
                                   "could not", "error:", "no serial"))), "")
            detail = re.sub(r"[^\\x20-\\x7e]", " ", detail)[:240]
            raise SetupError(
                f"USB {stage.lower()} failed on {port}."
                + (f" Esptool: {detail}" if detail else f" ({type(error).__name__})")
                + " Close serial monitors and try bootloader mode if connection failed."
            ) from error
    if full:
        if not (destination / "controller-nvs.bin").is_file():
            raise SetupError("Provisioning settings are required before full flash")
    writes = []
    for f in files:
        data = (destination / f["asset"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != f["sha256"]:
            raise SetupError("Prepared firmware checksum mismatch")
        writes += [hex(f["offset"]), str(destination / f["asset"])]
    if full:
        if (destination / "controller-nvs.bin").stat().st_size != profile["nvs"]["size"]:
            raise SetupError("Invalid NVS image size")
        run(args + ["erase_flash"], "Erase flash")
        writes += [hex(profile["nvs"]["offset"]), str(destination / "controller-nvs.bin")]
    else:
        table = next(f for f in profile["files"] if f["role"] == "partitions")
        expected = (destination / table["asset"]).read_bytes()
        with tempfile.TemporaryDirectory(prefix="controller-layout-") as temp:
            saved = Path(temp) / "partitions.bin"
            run(args + ["read_flash", "0x8000", "0x1000", str(saved)], "Read partition table")
            if saved.read_bytes()[:len(expected)] != expected:
                raise SetupError("Device partition layout differs; use new-device setup after saving settings")
    run(args + ["write_flash", "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "16MB"] + writes, "Write firmware")


def self_test():
    """Exercise bundled tools without hardware or network, including real NVS output."""
    import esptool
    import serial
    import tkinter
    profile = {"chip": "esp32s3", "provisioning_schema": "controller-v1",
               "nvs": {"offset": 0x9000, "size": 0x6000},
               "files": [{"role": role, "offset": offset, "asset": role + ".bin", "size": 1, "sha256": "0" * 64}
                         for role, offset in [("bootloader", 0), ("partitions", 0x8000), ("otadata", 0xf000), ("application", 0x20000)]]}
    with tempfile.TemporaryDirectory() as temp:
        provision({"wifi_ssid": "Self test", "wifi_pass": "test-password", "gateway_url": "wss://example.net/controller/device/ws", "device_token": "test-token"}, profile, temp)
        if (Path(temp) / "controller-nvs.bin").stat().st_size != 0x6000:
            raise RuntimeError("NVS self test failed")
    import sys
    if sys.platform == "win32":
        root = tkinter.Tk()
        root.withdraw()
        root.update()
        root.destroy()
    print("Bundled runtime self test passed")
