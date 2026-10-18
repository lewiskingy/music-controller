"""Execute the production C command policy on the host, without ESP-IDF."""
import ctypes
import subprocess
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parents[1]

@pytest.fixture(scope="module")
def policy(tmp_path_factory):
    folder = tmp_path_factory.mktemp("playback-policy")
    source = folder / "policy.c"
    source.write_text('#include "playback_policy.h"\n'
                      'const char *toggle(int playing) { return playback_toggle_command(playing); }\n'
                      'int step(int volume, int up) { return playback_volume_step(volume, up); }\n'
                      'int seek_ok(int duration, int live, int active, int allow, int gateway, int identity) { return playback_seek_allowed(duration, live, active, allow, gateway, identity); }\n'
                      'int seek_pos(int value, int duration) { return playback_seek_position(value, duration); }\n'
                      'int target(const char *p, const char *q, const char *i, const char *ep, const char *eq, const char *ei, int seek) { return playback_target_matches(p,q,i,ep,eq,ei,seek); }\n')
    library = folder / "policy.so"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                    "-I", str(ROOT / "firmware/esp32/main"), str(source), "-o", str(library)], check=True)
    loaded = ctypes.CDLL(str(library))
    loaded.toggle.restype = ctypes.c_char_p
    return loaded

def test_single_centre_control_never_sends_pause(policy):
    assert policy.toggle(1) == b"players/cmd/stop"
    assert policy.toggle(0) == b"players/cmd/play"

@pytest.mark.parametrize("volume,up,down", [(-20,5,0),(0,5,0),(12,15,10),
                                          (15,20,10),(99,100,95),(100,100,95),(200,100,95)])
def test_volume_boundaries(policy, volume, up, down):
    assert policy.step(volume, 1) == up
    assert policy.step(volume, 0) == down

def test_volume_steps_are_bounded_and_monotonic(policy):
    for volume in range(101):
        up, down = policy.step(volume, 1), policy.step(volume, 0)
        assert 0 <= down <= volume <= up <= 100
        assert up % 5 == 0 and down % 5 == 0
        if volume < 100: assert up > volume
        if volume > 0: assert down < volume


def test_seek_requires_a_finite_supported_active_track(policy):
    assert policy.seek_ok(220, 0, 1, 1, 1, 1) == 1
    for duration in [0, -1, 604801]: assert policy.seek_ok(duration, 0, 1, 1, 1, 1) == 0
    assert policy.seek_ok(220, 1, 1, 1, 1, 1) == 0
    for index in [2, 3, 4, 5]:
        args = [220, 0, 1, 1, 1, 1]
        args[index] = 0
        assert policy.seek_ok(*args) == 0

def test_seek_clamps_within_current_track(policy):
    assert policy.seek_pos(-10, 220) == 0
    assert policy.seek_pos(42, 220) == 42
    assert policy.seek_pos(220, 220) == 219
    assert policy.seek_pos(500, 220) == 219
    assert policy.seek_pos(20, 0) == 0

def test_stale_track_or_player_gestures_are_rejected(policy):
    baseline = [b"room-a", b"queue-a", b"item-a"]
    assert policy.target(*baseline, *baseline, 1) == 1
    for index in range(3):
        old = baseline.copy()
        old[index] = b"stale"
        assert policy.target(*baseline, *old, 1) == 0
    assert policy.target(*baseline, b"room-a", b"queue-b", b"item-b", 0) == 1
    assert policy.target(*baseline, b"room-b", b"queue-a", b"item-a", 0) == 0
