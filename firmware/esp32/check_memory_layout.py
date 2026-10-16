"""Verify bulk controller state is linked into ESP32-S3 PSRAM, not internal DRAM."""
import subprocess
import sys

names = {"ui", "players", "music_providers", "queue_items", "browse_stack", "browse_items", "search_items"}
output = subprocess.check_output(
    ["xtensa-esp32s3-elf-nm", "-S", "--defined-only", sys.argv[1]], text=True
)
found = {}
for line in output.splitlines():
    fields = line.split()
    if len(fields) == 4 and fields[3] in names:
        address, size, kind, name = fields
        assert kind.lower() == "b" and 0x3C000000 <= int(address, 16) < 0x3E000000, line
        found[name] = int(size, 16)
assert found.keys() == names, f"Missing external BSS symbols: {names - found.keys()}"
print(f"Verified {sum(found.values())} bytes of controller state in PSRAM ({len(found)} symbols)")
