from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/esp32"
MAIN = (FW / "main/main.c").read_text()
PROVISION = (FW / "provision.py").read_text()
GITIGNORE = (ROOT / ".gitignore").read_text()


def test_device_uses_provisioned_nvs_not_public_credentials():
    for name in ("wifi_ssid", "wifi_pass", "device_token", "gateway_url"):
        assert name in MAIN and name in PROVISION
    assert 'nvs_open("controller", NVS_READONLY' in MAIN
    assert 'firmware/esp32/controller-nvs.bin' in GITIGNORE
    assert 'X-Device-Token:' in MAIN
    assert "esp_crt_bundle_attach" in MAIN
    assert "wss://" in MAIN


def test_live_state_and_recovery():
    assert '"players/all"' in MAIN
    assert '"player_queues/all"' in MAIN
    assert 'gateway/ready' in MAIN
    assert 'WIFI_EVENT_STA_DISCONNECTED' in MAIN
    assert 'esp_websocket_client_start' in MAIN
    assert 'lvgl_port_lock' in MAIN
