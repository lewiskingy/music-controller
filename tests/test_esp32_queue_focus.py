from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "firmware/esp32/main/main.c").read_text()

def test_queue_is_a_separate_fullscreen_view():
    assert "VIEW_QUEUE" in SOURCE
    assert "lv_scr_load(queue_screen)" in SOURCE
    assert "lv_scr_load(now_screen)" in SOURCE
    assert "lv_obj_clear_flag(queue_screen, LV_OBJ_FLAG_SCROLLABLE)" in SOURCE
    assert "lv_list_create(queue_screen)" in SOURCE
    assert "queue_back" in SOURCE

def test_queue_commands_and_bounded_pagination():
    assert '"player_queues/items"' in SOURCE
    assert '"player_queues/play_index"' in SOURCE
    assert "#define QUEUE_PAGE_SIZE 30" in SOURCE
    assert 'cJSON_AddNumberToObject(args, "offset", queue_offset)' in SOURCE
    assert 'cJSON_AddNumberToObject(args, "index", index)' in SOURCE
    assert "queue_offset >= QUEUE_PAGE_SIZE" in SOURCE
    assert "queue_has_more" in SOURCE
    assert "Discarding stale queue page" in SOURCE

def test_selected_player_scope():
    assert 'snprintf(queue_target, sizeof(queue_target), "%s", player_id)' in SOURCE
    assert 'strcmp(queue_request_target, queue_target)' in SOURCE
    assert "queue_current_index" in SOURCE
