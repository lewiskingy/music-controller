from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "firmware/esp32/main/main.c").read_text()
UI = (ROOT / "firmware/esp32/main/controller_ui.c").read_text()

def test_views_share_one_shell_and_dock():
    assert "controller_ui_create(&ui, now_screen" in SOURCE
    assert "controller_ui_show(&ui, UI_PLAYERS)" in SOURCE
    assert "controller_ui_show(&ui, UI_PLAYING)" in SOURCE
    assert "controller_ui_show(&ui, UI_PROVIDERS)" in SOURCE
    assert "lv_scr_load" not in SOURCE
    assert "LV_OBJ_FLAG_HIDDEN" in UI
    assert "UI_DOCK_Y, UI_WIDTH, UI_DOCK_HEIGHT" in UI

def test_persist_selection_and_avoid_secrets():
    assert 'nvs_set_str(n, "player_id", id)' in SOURCE
    assert "nvs_commit(n)" in SOURCE
    assert 'READ("device_token",token)' in SOURCE

def test_commands_capture_player_and_recover_pending():
    assert "command_with_args(command, action->player_id" in SOURCE
    assert "!strcmp(player_id, action->player_id)" in SOURCE
    assert "command_pending || !ready || !current_available" in SOURCE
    assert "!strcmp(mid->valuestring, pending_command_id)" in SOURCE
    assert "now - command_started > 10000000LL" in SOURCE
    assert "finish_command()" in SOURCE
    assert "players/cmd/pause" not in SOURCE
