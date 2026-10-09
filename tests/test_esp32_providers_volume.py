from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "firmware/esp32/main/main.c").read_text()

def test_provider_screen_is_exclusive_and_selection_persistent():
    assert "VIEW_PROVIDERS" in SOURCE
    assert "controller_ui_show(&ui, UI_PROVIDERS)" in SOURCE
    assert "lv_checkbox_create(providers_list)" in SOURCE
    assert 'nvs_set_str(n, "music_sources", value)' in SOURCE
    assert 'request("providers", \'m\')' in SOURCE
    assert 'strcmp(type->valuestring, "music")' in SOURCE
    assert "provider_fetch_pending = true" in SOURCE
