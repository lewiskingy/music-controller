"""PyInstaller entry point (absolute imports work in script and frozen modes)."""
import io
import sys
# PyInstaller's windowed executable has no standard streams. Third-party flashing
# tools still print progress; the GUI reports stages independently.
if sys.stdout is None:
    sys.stdout = io.StringIO()
if sys.stderr is None:
    sys.stderr = io.StringIO()
from music_controller_setup.cli import main

if __name__ == "__main__":
    main()
