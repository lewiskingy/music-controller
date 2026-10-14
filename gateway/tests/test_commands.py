import importlib.util
from pathlib import Path
import pytest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

SPEC = importlib.util.spec_from_file_location("controller_gateway", Path(__file__).resolve().parents[1] / "app.py")
GATEWAY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATEWAY)

@pytest.mark.parametrize("position", [0, 42, 604800])
def test_seek_accepts_integer_seconds(position):
    args = {"queue_id": "extension", "position": position}
    assert GATEWAY.validate_command("player_queues/seek", args) == args

@pytest.mark.parametrize("args", [
    {"queue_id": "extension"}, {"position": 20},
    {"queue_id": "", "position": 20}, {"queue_id": "x" * 513, "position": 20},
    {"queue_id": "extension", "position": True},
    {"queue_id": "extension", "position": 1.5},
    {"queue_id": "extension", "position": -1},
    {"queue_id": "extension", "position": 604801},
    {"queue_id": "extension", "position": 42, "player_id": "other"},
])
def test_seek_rejects_malformed_or_unbounded_requests(args):
    with pytest.raises(ValueError): GATEWAY.validate_command("player_queues/seek", args)

def test_existing_volume_and_transport_rules_preserved():
    assert GATEWAY.validate_command("players/cmd/volume_set", {"player_id": "extension", "volume_level": 45})
    for command in ["auth/logout", "config/set", "players/delete", "player_queues/unknown"]:
        with pytest.raises(ValueError): GATEWAY.validate_command(command, {})
    for value in [-1, 101, True, 5.5]:
        with pytest.raises(ValueError):
            GATEWAY.validate_command("players/cmd/volume_set", {"player_id": "extension", "volume_level": value})

def test_gateway_advertises_seek_support():
    assert GATEWAY.DEVICE_CAPABILITIES == {"seek": True, "artwork_rgb565": True, "queue_item_play": True, "browse": True, "search": True}

@pytest.mark.parametrize("index", [0, 100000, "stable-queue-item-id"])
def test_queue_item_play_accepts_index_or_stable_identity(index):
    assert GATEWAY.validate_command("player_queues/play_index", {"queue_id":"room", "index":index})

@pytest.mark.parametrize("index", [True, -1, 100001, "", "x"*513, 1.5, {}, None])
def test_queue_item_play_rejects_invalid_target(index):
    with pytest.raises(ValueError):
        GATEWAY.validate_command("player_queues/play_index", {"queue_id":"room", "index":index})

@pytest.mark.parametrize("args", [{}, {"queue_id":"room","limit":0}, {"queue_id":"room","limit":101}, {"queue_id":"room","offset":-1}])
def test_queue_page_rejects_invalid_pagination(args):
    with pytest.raises(ValueError): GATEWAY.validate_command("player_queues/items",args)
