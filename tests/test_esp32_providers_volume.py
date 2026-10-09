from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "firmware/esp32/main/main.c").read_text()

def test_volume_snaps_to_next_five_in_pressed_direction():
    assert "volume <= 0 ? 0 : ((volume - 1) / 5) * 5" in SOURCE
    assert "volume >= 100 ? 100 : ((volume / 5) + 1) * 5" in SOURCE
    for value, up, down in [(0,5,0),(12,15,10),(15,20,10),(99,100,95),(100,100,95)]:
        assert up == (100 if value >= 100 else (value // 5 + 1) * 5)
        assert down == (0 if value <= 0 else ((value - 1) // 5) * 5)

def test_provider_screen_is_exclusive_and_selection_persistent():
    assert "VIEW_PROVIDERS" in SOURCE
    assert "lv_scr_load(providers_screen)" in SOURCE
    assert "lv_checkbox_create(providers_list)" in SOURCE
    assert 'nvs_set_str(n, "music_sources", value)' in SOURCE
    assert 'request("providers", \'m\')' in SOURCE
    assert 'strcmp(type->valuestring, "music")' in SOURCE
    assert "provider_fetch_pending = true" in SOURCE
