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
                      'int step(int volume, int up) { return playback_volume_step(volume, up); }\n')
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
