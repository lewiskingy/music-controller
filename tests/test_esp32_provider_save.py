from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "firmware/esp32/main/main.c").read_text()

def test_checkbox_change_does_not_write_flash():
    start = SOURCE.index("if (action->type == ACT_PROVIDER_TOGGLE)")
    end = SOURCE.index("if (action->type == ACT_SELECT)", start)
    assert "save_music_providers()" not in SOURCE[start:end]
    assert "awaiting Save" in SOURCE[start:end]

def test_explicit_save_and_feedback():
    assert "ACT_PROVIDER_SAVE" in SOURCE
    assert "save_providers_clicked" in SOURCE
    assert 'lv_label_set_text(save_text, "Save")' in SOURCE
    assert 'lv_label_set_text(provider_save_status, "Selection saved")' in SOURCE
    assert 'nvs_commit(n)' in SOURCE
    assert 'esp_reset_reason()' in SOURCE
