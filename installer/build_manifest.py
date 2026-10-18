"""Package the build-produced flash inputs, checking partition compatibility."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil


def build(build_dir, destination, version):
    build_dir, destination = Path(build_dir), Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    flash = json.loads((build_dir / "flasher_args.json").read_text())
    expected = {0: "bootloader", 0x8000: "partitions", 0xf000: "otadata", 0x20000: "application"}
    actual = {int(offset, 0): name for offset, name in flash["flash_files"].items()}
    if set(actual) != set(expected):
        raise ValueError("Build flash layout changed; update installer compatibility checks first")
    partitions = Path(__file__).resolve().parents[1] / "firmware/esp32/partitions.csv"
    with partitions.open() as stream:
        rows = {row[0].strip(): [c.strip() for c in row] for row in csv.reader(line for line in stream if not line.startswith("#")) if row}
    if rows["nvs"][3:5] != ["0x9000", "0x6000"] or rows["ota_0"][3] != "0x20000":
        raise ValueError("Partition table incompatible with controller-v1")
    files = []
    for offset, role in expected.items():
        source = build_dir / actual[offset]
        name = "waveshare-4.3-" + role + ".bin"
        target = destination / name
        shutil.copyfile(source, target)
        data = target.read_bytes()
        files.append(dict(role=role, asset=name, offset=offset, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    profile = dict(id="waveshare-esp32s3-touch-lcd-4.3", name="Waveshare ESP32-S3 Touch LCD 4.3 (800×480)", chip="esp32s3", resolution=[800, 480], provisioning_schema="controller-v1", nvs=dict(offset=0x9000, size=0x6000), files=files)
    manifest = dict(schema_version=1, version=version, installer=dict(minimum_version="0.5.0"), gateway=dict(source_tag=version), profiles=[profile])
    (destination / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir")
    parser.add_argument("destination")
    parser.add_argument("version")
    args = parser.parse_args()
    build(args.build_dir, args.destination, args.version)
