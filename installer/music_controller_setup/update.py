"""Windows single-instance enforcement and verified, opt-in self-updates."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from urllib.request import Request, urlopen

from . import core

# Generated during the Windows release build; source/development runs don't self-update.
try:
    from ._build_version import VERSION
except ImportError:
    VERSION = "0.0.0"

ASSET = "music-controller-setup.exe"
MUTEX_NAME = r"Local\MusicControllerSetup-6ED92049-B5E8-42D8-97F7-B9D5F147CABA"
_handle = None


def version_tuple(value):
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", value or "")
    return tuple(map(int, match.groups())) if match else None


def single_instance():
    """Hold named mutex for entire GUI lifetime, never PID-file/race based."""
    global _handle
    if sys.platform != "win32":
        return True
    kernel = ctypes.windll.kernel32
    kernel.CreateMutexW.restype = ctypes.c_void_p
    handle = kernel.CreateMutexW(None, False, MUTEX_NAME)
    if not handle:
        raise OSError("Could not create application mutex")
    if kernel.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
        kernel.CloseHandle(ctypes.c_void_p(handle))
        return False
    _handle = handle
    return True


def release_info():
    """Only GitHub's latest *published stable* release, never rolling drafts."""
    if not getattr(sys, "frozen", False) or VERSION == "0.0.0" or version_tuple(VERSION) is None:
        return None
    current = version_tuple(VERSION)
    release = json.loads(core.download(core.API + "/releases/latest", 1024 * 1024))
    if release.get("draft") or release.get("prerelease"):
        return None
    newer = version_tuple(release.get("tag_name"))
    if newer is None or newer <= current:
        return None
    asset = next((a for a in release.get("assets", []) if a.get("name") == ASSET), None)
    if not asset:
        return None
    digest = asset.get("digest", "")
    if not re.fullmatch(r"sha256:[a-f0-9]{64}", digest):
        # Fail closed on integrity metadata rather than trusting a mutable URL.
        return None
    return {"version": release["tag_name"], "url": asset["browser_download_url"],
            "sha256": digest.split(":", 1)[1]}


def minimum_version(manifest):
    required = manifest.get("installer", {}).get("minimum_version", "0.0.0")
    if version_tuple(required) is None:
        raise core.SetupError("Invalid minimum installer version in release manifest")
    return required


def compatible(manifest):
    return version_tuple(VERSION) is not None and version_tuple(VERSION) >= version_tuple(minimum_version(manifest))


def stage(info):
    """Never execute until SHA-256 from GitHub's release asset metadata matches."""
    data = core.download(info["url"], limit=100 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != info["sha256"]:
        raise core.SetupError("Installer update SHA-256 verification failed")
    folder = Path(tempfile.mkdtemp(prefix="music-controller-update-"))
    path = folder / ASSET
    path.write_bytes(data)
    return path


def begin_update(path):
    """Updater runs from temp; original exits before its executable is replaced."""
    if sys.platform != "win32" or not getattr(sys, "frozen", False):
        raise core.SetupError("Self-update is only available in the installed Windows executable")
    subprocess.Popen([str(path), "--apply-update", str(Path(sys.executable).resolve()),
                      "--old-pid", str(os.getpid())], close_fds=True,
                     creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)


def apply_update(destination, old_pid):
    """Launch external Windows helper; running EXEs cannot reliably replace themselves."""
    if sys.platform != "win32" or not getattr(sys, "frozen", False):
        return 1
    source = Path(sys.executable).resolve()
    destination = Path(destination).resolve()
    if source.name.lower() != ASSET or destination.name.lower() != ASSET:
        return 1
    if not source.parent.name.startswith("music-controller-update-"):
        return 1
    if old_pid is None or old_pid <= 0:
        return 1
    # This process and the old installer both need to exit before Windows will
    # allow replacement. PowerShell is only a small local handoff helper.
    helper = source.parent / "finish-update.ps1"
    helper.write_text("""
param([string]$Source, [string]$Destination, [int]$OldPid, [int]$StagedPid)
foreach ($id in @($OldPid, $StagedPid)) {
    try { Wait-Process -Id $id -Timeout 45 -ErrorAction Stop }
    catch { }
}
$success = $false
for ($attempt = 0; $attempt -lt 60; $attempt++) {
    try {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
        $success = $true
        break
    } catch { Start-Sleep -Milliseconds 500 }
}
if ($success) { Start-Process -FilePath $Destination }
""", encoding="utf-8")
    subprocess.Popen(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
         "-File", str(helper), "-Source", str(source),
         "-Destination", str(destination), "-OldPid", str(old_pid),
         "-StagedPid", str(os.getpid())],
        close_fds=True, creationflags=subprocess.CREATE_NO_WINDOW)
    return 0
