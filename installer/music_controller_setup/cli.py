"""Command line entry point; no arguments opens the Windows GUI."""
import argparse
import getpass
from pathlib import Path
import tempfile
from . import core, update


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=["releases", "install", "provision"])
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--apply-update", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--old-pid", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--rel")
    parser.add_argument("--profile")
    parser.add_argument("--port")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--keep-files", type=Path, metavar="DIRECTORY")
    parser.add_argument("--export-gateway", type=Path, metavar="FILE")
    args = parser.parse_args(argv)
    if args.apply_update:
        return update.apply_update(args.apply_update, args.old_pid)
    if args.mode is None and not update.single_instance():
        from tkinter import messagebox
        messagebox.showinfo("Music Controller Setup", "Music Controller Setup is already running.")
        return
    if args.self_test:
        core.self_test()
        return
    if args.mode is None:
        from .gui import run
        run()
        return
    try:
        if args.mode == "releases":
            for r in core.releases():
                print(r["tag_name"], r["published_at"], r["name"])
            return
        if args.prepare_only and not args.keep_files:
            parser.error("--prepare-only requires --keep-files DIRECTORY")
        selected = core.release(args.rel)
        data = core.manifest(selected)
        if update.VERSION != "0.0.0" and not update.compatible(data):
            raise core.SetupError("Installer update required: version " + update.minimum_version(data))
        profile = core.profile_for(data, args.profile)
        settings = None
        if args.mode == "provision":
            settings = {
                "wifi_ssid": input("Wi-Fi SSID: "),
                "wifi_pass": getpass.getpass("Wi-Fi password: "),
                "gateway_url": input("Gateway WSS URL: ").strip(),
                "device_token": getpass.getpass("Existing gateway key (blank generates a new key): ").strip() or core.new_key(),
                "player_id": input("Player ID (optional): ").strip(),
            }
            sleep_answer = input("Enable Wi-Fi modem sleep to save power? [y/N]: ").strip().lower()
            if sleep_answer not in ("", "n", "no", "y", "yes"):
                raise core.SetupError("Wi-Fi sleep must be yes or no.")
            settings["wifi_sleep"] = sleep_answer in ("y", "yes")
            core.validate_settings(settings)
            if args.export_gateway:
                core.export_gateway_key(settings["device_token"], args.export_gateway)
                print("Private gateway configuration exported; apply it to the gateway and redeploy.")
            else:
                print("Gateway key (keep private):", settings["device_token"])
        port = args.port
        if not args.prepare_only and not port:
            candidates = core.ports()
            if len(candidates) != 1:
                parser.error("Select --port from: " + ", ".join(p[0] for p in candidates))
            port = candidates[0][0]
        if not args.prepare_only:
            action = "ERASE all device data and provision" if settings else "update firmware, preserving settings"
            print(f"{selected['tag_name']} / {profile['name']} / {port}: {action}")
            if input("Type FLASH to continue: ") != "FLASH":
                return
        with tempfile.TemporaryDirectory(prefix="music-controller-") as temp:
            destination = args.keep_files or Path(temp)
            if args.keep_files:
                if settings:
                    print("Retained NVS contains Wi-Fi and gateway credentials. Keep the output directory private.")
                destination.mkdir(parents=True, exist_ok=False)
            core.prepare(selected, profile, destination)
            if settings:
                core.provision(settings, profile, destination)
            if not args.prepare_only:
                core.flash(profile, destination, port, full=bool(settings))
            print("Complete.")
            if args.keep_files:
                print("Retained files:", destination, "— provisioning files contain credentials.")
    except core.SetupError as error:
        parser.exit(1, f"{error}\n")
    except Exception as error:
        # Third-party errors may contain inputs. Give useful type/context without dumping secrets.
        parser.exit(1, f"Installation failed ({type(error).__name__}). Check release, USB connection and settings.\n")
