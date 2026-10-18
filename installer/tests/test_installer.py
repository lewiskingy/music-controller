import hashlib
import json
from pathlib import Path
from unittest.mock import patch
import pytest
from music_controller_setup import core
from build_manifest import build


@pytest.fixture
def package(tmp_path):
    roles = {"bootloader": 0, "partitions": 0x8000, "otadata": 0xf000, "application": 0x20000}
    files = []
    for role, offset in roles.items():
        payload = (role + "-payload").encode()
        (tmp_path / (role + ".bin")).write_bytes(payload)
        files.append(dict(role=role, offset=offset, asset=role + ".bin", size=len(payload), sha256=hashlib.sha256(payload).hexdigest()))
    return tmp_path, dict(id="board", chip="esp32s3", provisioning_schema="controller-v1", nvs=dict(offset=0x9000, size=0x6000), files=files)


def device_stub(path, calls):
    def run(args):
        calls.append(args)
        if "read_flash" in args:
            Path(args[-1]).write_bytes((path / "partitions.bin").read_bytes())
    return run


def test_update_preserves_nvs_and_checks_layout(package):
    path, profile = package
    calls = []
    with patch("esptool.main", side_effect=device_stub(path, calls)):
        core.flash(profile, path, "COM4")
    assert "read_flash" in calls[0]
    write = calls[-1]
    assert "0xf000" in write and "0x20000" in write
    assert "0x9000" not in write and "0x8000" not in write
    assert not any("erase_flash" in call for call in calls)


def test_layout_mismatch_rejects_before_write(package):
    path, profile = package
    def run(args):
        Path(args[-1]).write_bytes(b"wrong-layout")
    with patch("esptool.main", side_effect=run) as tool, pytest.raises(ValueError, match="partition layout"):
        core.flash(profile, path, "COM4")
    assert tool.call_count == 1


def test_full_flash_requires_nvs_and_verifies_before_erase(package):
    path, profile = package
    with patch("esptool.main") as tool:
        with pytest.raises(ValueError, match="Provisioning"):
            core.flash(profile, path, "COM4", True)
        (path / "controller-nvs.bin").write_bytes(b"\xff" * 0x6000)
        (path / "application.bin").write_bytes(b"corrupt")
        with pytest.raises(ValueError, match="checksum"):
            core.flash(profile, path, "COM4", True)
        tool.assert_not_called()


def test_full_flash_writes_all_partitions(package):
    path, profile = package
    (path / "controller-nvs.bin").write_bytes(b"\xff" * 0x6000)
    with patch("esptool.main") as tool:
        core.flash(profile, path, "COM4", True)
    assert "erase_flash" in tool.call_args_list[0].args[0]
    assert "0x9000" in tool.call_args_list[1].args[0]


def test_real_nvs_generator_and_csv_escaping(package):
    path, profile = package
    settings = dict(wifi_ssid='WiFi, "home"', wifi_pass="secret", gateway_url="wss://example.net/controller/device/ws", device_token="abc123", player_id="living-room")
    core.provision(settings, profile, path)
    data = (path / "controller-nvs.bin").read_bytes()
    assert len(data) == 0x6000
    for value in settings.values():
        assert value.encode() in data
    assert not list(path.glob("*.csv"))


def test_manifest_and_profile_selection(package):
    path, profile = package
    selected = dict(tag_name="v0.5.0", assets=[dict(name=core.MANIFEST, browser_download_url="https://github.com/manifest")])
    with patch.object(core, "download", return_value=json.dumps(dict(schema_version=1, version="v0.5.0", profiles=[profile])).encode()):
        assert core.profile_for(core.manifest(selected)) == profile
    with pytest.raises(ValueError):
        core.profile_for(dict(profiles=[profile, dict(id="other")]))


def test_corrupt_download_is_rejected(package):
    path, profile = package
    selected = dict(tag_name="v0.5.0", assets=[dict(name=f["asset"], browser_download_url="https://github.com/file") for f in profile["files"]])
    with patch.object(core, "download", return_value=b"corrupt"), pytest.raises(ValueError, match="mismatch"):
        core.prepare(selected, profile, path / "download")


@pytest.mark.parametrize("url", ["http://github.com/file", "https://evil.example/file", "file:///tmp/secret"])
def test_download_origin_is_restricted(url):
    with pytest.raises(ValueError):
        core.download(url)


def test_release_discovery_filters_old_releases():
    releases = [dict(tag_name="gateway-v1.0.0", draft=False, assets=[]), dict(tag_name="v0.5.0", draft=False, assets=[dict(name=core.MANIFEST)])]
    with patch.object(core, "download", return_value=json.dumps(releases).encode()):
        assert [r["tag_name"] for r in core.releases()] == ["v0.5.0"]


def test_settings_limits_and_secure_url():
    settings = dict(wifi_ssid="wifi", device_token=core.new_key(), gateway_url="wss://example.net/ws")
    core.validate_settings(settings)
    for key, value in [("gateway_url", "ws://example.net/ws"), ("gateway_url", "wss://"), ("wifi_ssid", "é" * 17), ("device_token", "bad\0key")]:
        with pytest.raises(ValueError):
            core.validate_settings({**settings, key: value})


def test_export_is_explicit_and_never_overwrites(tmp_path):
    path = tmp_path / "gateway.env"
    core.export_gateway_key("abc$123", path)
    assert path.read_text() == "CONTROLLER_DEVICE_TOKEN='abc$123'\n"
    with pytest.raises(FileExistsError):
        core.export_gateway_key("new", path)


def test_build_manifest_uses_actual_flash_inputs(tmp_path):
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    flash_files = {}
    for offset in [0, 0x8000, 0xf000, 0x20000]:
        name = f"{offset}.bin"
        (build_dir / name).write_bytes(str(offset).encode())
        flash_files[hex(offset)] = name
    (build_dir / "flasher_args.json").write_text(json.dumps(dict(flash_files=flash_files)))
    output = tmp_path / "out"
    build(build_dir, output, "v0.5.0")
    manifest = json.loads((output / core.MANIFEST).read_text())
    core.validate_profile(core.profile_for(manifest))
    assert manifest["gateway"]["source_tag"] == "v0.5.0"
    flash_files["0x9000"] = "unexpected.bin"
    (build_dir / "flasher_args.json").write_text(json.dumps(dict(flash_files=flash_files)))
    with pytest.raises(ValueError, match="layout changed"):
        build(build_dir, output, "v0.5.0")


def test_bundled_runtime_self_test():
    core.self_test()


def test_flash_tool_failure_is_actionable(package):
    path, profile = package
    with patch("esptool.main", side_effect=SystemExit(2)), pytest.raises(core.SetupError, match="bootloader mode"):
        core.flash(profile, path, "COM4")


@pytest.mark.parametrize("sleep_answer", ["", "yes", "no"])
def test_prepare_only_keeps_private_files_without_flashing(tmp_path, package, sleep_answer):
    from music_controller_setup.cli import main
    path, profile = package
    selected = dict(tag_name="v0.5.0")
    destination = tmp_path / "prepared"
    answers = iter(["Home WiFi", "wss://example.net/ws", "", sleep_answer])
    secrets = iter(["password", "existing-key"])
    with patch.object(core, "release", return_value=selected), patch.object(core, "manifest", return_value=dict(profiles=[profile])), patch.object(core, "prepare"), patch.object(core, "flash") as flashing, patch("builtins.input", side_effect=lambda _: next(answers)), patch("getpass.getpass", side_effect=lambda _: next(secrets)):
        main(["provision", "--prepare-only", "--keep-files", str(destination)])
    flashing.assert_not_called()
    assert (destination / "controller-nvs.bin").stat().st_size == 0x6000


def test_failed_install_removes_temporary_downloads(package):
    from music_controller_setup.cli import main
    path, profile = package
    used = []
    def prepare(selected, target, destination):
        used.append(Path(destination))
        (Path(destination) / "download.bin").write_bytes(b"temporary")
        raise core.SetupError("Download interrupted")
    with patch.object(core, "release", return_value=dict(tag_name="v0.5.0")), patch.object(core, "manifest", return_value=dict(profiles=[{**profile, "name": "test"}])), patch.object(core, "prepare", side_effect=prepare), patch("builtins.input", return_value="FLASH"), pytest.raises(SystemExit):
        main(["install", "--port", "COM4"])
    assert used and not used[0].exists()


def test_windows_gui_initialization(package):
    import sys
    if sys.platform != "win32":
        pytest.skip("Windows GUI acceptance runs on Windows CI")
    import time
    import tkinter as tk
    from music_controller_setup.gui import Installer
    path, profile = package
    profile["name"] = "Test hardware"
    selected = dict(tag_name="v0.5.0", prerelease=False)
    root = tk.Tk()
    try:
        with patch.object(core, "releases", return_value=[selected]), patch.object(core, "ports", return_value=[("COM4", "Test USB")]), patch.object(core, "manifest", return_value=dict(profiles=[profile])):
            app = Installer(root)
            deadline = time.monotonic() + 5
            while (app.busy or app.data is None) and time.monotonic() < deadline:
                root.update()
                time.sleep(0.02)
            assert app.data is not None
            assert app.profile.get() == "Test hardware"
            assert app.port.get().startswith("COM4")
            assert app.mode.get() == "Update existing device"
            assert app.status.get().startswith("Ready")
            assert app.settings["wifi_sleep"].get() is False
            assert root.winfo_reqheight() <= 740
    finally:
        root.destroy()


@pytest.mark.parametrize("setting, expected", [(None, 0), (False, 0), (True, 1)])
def test_wifi_sleep_nvs_value(package, setting, expected):
    path, profile = package
    settings = dict(wifi_ssid="wifi", device_token="key", gateway_url="wss://example.net/ws")
    if setting is not None:
        settings["wifi_sleep"] = setting
    core.provision(settings, profile, path)
    data = (path / "controller-nvs.bin").read_bytes()
    # NVS entries are 32 bytes: type at byte 1, key at 8, inline u8 at 24.
    entries = [data[i:i + 32] for i in range(0, len(data), 32)]
    entry, = [e for e in entries if e[8:24].rstrip(b"\0") == b"wifi_sleep"]
    assert entry[1] == 0x01
    assert entry[24] == expected


@pytest.mark.parametrize("value", ["yes", "0", 1, None])
def test_wifi_sleep_rejects_non_boolean(value):
    with pytest.raises(core.SetupError, match="Wi-Fi sleep"):
        core.validate_settings(dict(wifi_sleep=value))


def test_usb_failure_reports_failing_stage(package):
    path, profile = package
    def fails(args):
        if "read_flash" in args:
            print("A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.")
            raise SystemExit(2)
    with patch("esptool.main", side_effect=fails), pytest.raises(core.SetupError, match="read partition table failed on COM4.*Failed to connect"):
        core.flash(profile, path, "COM4")


def test_usb_stage_progress_is_exposed(package):
    path, profile = package
    calls, stages = [], []
    with patch("esptool.main", side_effect=device_stub(path, calls)):
        core.flash(profile, path, "COM4", log=stages.append)
    assert stages == ["Read partition table…", "Write firmware…"]


def test_manual_bootloader_preserves_download_mode_between_flash_operations(package):
    path, profile = package
    calls = []
    with patch("esptool.main", side_effect=device_stub(path, calls)):
        core.flash(profile, path, "COM4", manual_bootloader=True)
    assert len(calls) == 2
    for args in calls:
        assert args[args.index("--baud") + 1] == "115200"
        assert args[args.index("--before") + 1] == "no-reset"
        assert args[args.index("--after") + 1] == "no-reset"
        assert "erase_flash" not in args
        assert "0x9000" not in args
    assert "read_flash" in calls[0] and "write_flash" in calls[1]


def test_normal_flash_keeps_default_reset_behaviour(package):
    path, profile = package
    calls = []
    with patch("esptool.main", side_effect=device_stub(path, calls)):
        core.flash(profile, path, "COM4")
    assert all("--before" not in call and "--after" not in call for call in calls)
    assert all(call[call.index("--baud") + 1] == "460800" for call in calls)
