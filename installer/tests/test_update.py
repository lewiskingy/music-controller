"""Self-update version, release selection and compatibility tests."""
from unittest.mock import patch
import pytest
from music_controller_setup import update


@pytest.mark.parametrize(("value", "expected"), [
    ("0.5.6", (0, 5, 6)), ("v1.20.3", (1, 20, 3)),
    ("main", None), ("v0.5.6-rc1", None), ("0.5", None),
])
def test_version(value, expected):
    assert update.version_tuple(value) == expected


def test_compatibility():
    with patch.object(update, "VERSION", "0.5.6"):
        assert update.compatible({"installer": {"minimum_version": "0.5.6"}})
        assert not update.compatible({"installer": {"minimum_version": "0.5.7"}})
        assert update.compatible({})


def test_latest_stable_and_digest_required():
    release = {
        "tag_name": "v0.5.7", "draft": False, "prerelease": False,
        "assets": [{"name": update.ASSET, "browser_download_url":
                    "https://github.com/lewiskingy/music-controller/releases/download/v0.5.7/music-controller-setup.exe",
                    "digest": "sha256:" + "a" * 64}],
    }
    with patch.object(update, "VERSION", "0.5.6"), patch.object(update.sys, "frozen", True, create=True), patch.object(update.core, "download",
            return_value=__import__("json").dumps(release).encode()):
        assert update.release_info()["version"] == "v0.5.7"
        release["assets"][0]["digest"] = ""
        assert update.release_info() is None


def test_stage_rejects_mismatched_digest():
    with patch.object(update.core, "download", return_value=b"untrusted"):
        with pytest.raises(update.core.SetupError):
            update.stage({"url": "https://github.com/example", "sha256": "a" * 64})
