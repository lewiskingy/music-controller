"""Tk interface. Network and flashing work run outside the UI thread."""
from pathlib import Path
import queue
import tempfile
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from . import core, update


class Installer:
    def __init__(self, root):
        self.root = root
        self.events = queue.Queue()
        self.busy = False
        self.selected = None
        self.data = None
        self.available = []
        root.title(f"Music Controller Setup {update.VERSION}")
        root.geometry("720x740")
        root.minsize(680, 740)
        root.protocol("WM_DELETE_WINDOW", self.close)
        frame = ttk.Frame(root, padding=20)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Music Controller", font=("Segoe UI", 20)).pack(anchor="w")
        ttk.Label(frame, text="Connect your controller by USB, then choose a release.").pack(anchor="w", pady=8)
        self.controls = []
        self.release = self.combo(frame, "Release", self.select_release)
        self.profile = self.combo(frame, "Hardware")
        self.port = self.combo(frame, "USB port")
        self.button(frame, "Refresh releases and ports", self.refresh)
        self.mode = tk.StringVar(value="Update existing device")
        mode = ttk.Combobox(frame, textvariable=self.mode, state="readonly", values=["Update existing device", "Set up new device"])
        mode.pack(fill="x", pady=8)
        self.controls.append(mode)
        self.settings = {}
        form = ttk.Frame(frame)
        form.pack(fill="x", pady=8)
        form.columnconfigure(1, weight=1)
        for index, (key, label, secret) in enumerate([
            ("wifi_ssid", "Wi-Fi SSID", False), ("wifi_pass", "Wi-Fi password", True),
            ("gateway_url", "Gateway URL (wss://host/controller/device/ws)", False),
            ("device_token", "Gateway key (reuse existing key)", True),
            ("player_id", "Music Assistant player ID (optional)", False),
        ]):
            ttk.Label(form, text=label).grid(row=index, column=0, sticky="w", padx=(0, 12), pady=5)
            variable = tk.StringVar()
            entry = ttk.Entry(form, textvariable=variable, show="*" if secret else "")
            entry.grid(row=index, column=1, sticky="ew", pady=5)
            self.settings[key] = variable
            self.controls.append(entry)
        self.settings["wifi_sleep"] = tk.BooleanVar(value=False)
        wifi_sleep = ttk.Checkbutton(
            form, text="Enable Wi-Fi sleep (saves battery; may add latency)",
            variable=self.settings["wifi_sleep"])
        wifi_sleep.grid(row=5, column=0, columnspan=2, sticky="w", pady=5)
        self.controls.append(wifi_sleep)
        ttk.Label(form, text="Setup only. Update preserves the device’s existing settings.").grid(
            row=6, column=0, columnspan=2, sticky="w")
        row = ttk.Frame(frame)
        row.pack(fill="x")
        self.button(row, "Generate new gateway key", lambda: self.settings["device_token"].set(core.new_key()))
        self.button(row, "Copy key", lambda: (root.clipboard_clear(), root.clipboard_append(self.settings["device_token"].get())))
        self.button(row, "Export gateway setting…", self.export)
        for button in row.winfo_children():
            button.pack_configure(side="left", expand=True, padx=2)
        self.button(frame, "Install selected release…", lambda: self.install(False))
        self.button(frame, "Prepare files only…", lambda: self.install(True))
        self.progress = ttk.Progressbar(frame, mode="indeterminate")
        self.progress.pack(fill="x", pady=8)
        self.status = tk.StringVar(value="Loading releases…")
        ttk.Label(frame, textvariable=self.status, wraplength=600).pack(anchor="w")
        root.after(100, self.poll)
        self.refresh()
        threading.Thread(target=self.check_update, daemon=True).start()

    def check_update(self):
        try:
            info = update.release_info()
            if info:
                self.events.put((self.update_available, info))
        except Exception:
            pass  # offline use remains available

    def update_available(self, info):
        if self.busy:
            self.root.after(1200, lambda: self.update_available(info))
            return
        if messagebox.askyesno("Setup update available",
                f"Music Controller Setup {info['version']} is available.\n"
                f"Current installer: {update.VERSION}.\n\nUpdate now?"):
            self.task(lambda: update.stage(info), self.launch_update)

    def launch_update(self, path):
        try:
            update.begin_update(path)
        except Exception as error:
            return self.failed("Could not start verified update: " + type(error).__name__)
        self.root.destroy()

    def combo(self, parent, label, callback=None):
        ttk.Label(parent, text=label).pack(anchor="w")
        box = ttk.Combobox(parent, state="readonly")
        box.pack(fill="x", pady=(0, 6))
        if callback:
            box.bind("<<ComboboxSelected>>", callback)
        self.controls.append(box)
        return box

    def button(self, parent, label, command):
        button = ttk.Button(parent, text=label, command=command)
        button.pack(fill="x", pady=2)
        self.controls.append(button)

    def task(self, work, done):
        if self.busy:
            return
        self.busy = True
        for control in self.controls:
            control.configure(state="disabled")
        self.progress.start()
        def worker():
            try:
                result = work()
                self.events.put((done, result))
            except Exception as error:
                self.events.put((self.failed, str(error) if isinstance(error, core.SetupError) else f"{type(error).__name__}: check network, selected release, USB port and settings"))
        threading.Thread(target=worker, daemon=True).start()

    def poll(self):
        try:
            while True:
                callback, value = self.events.get_nowait()
                if callback is None:
                    self.status.set(value)
                    continue
                self.busy = False
                self.progress.stop()
                for control in self.controls:
                    control.configure(state="readonly" if isinstance(control, ttk.Combobox) else "normal")
                callback(value)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def failed(self, message):
        self.status.set("Operation failed: " + message)
        messagebox.showerror("Operation failed", self.status.get())

    def refresh(self):
        self.task(lambda: (core.releases(), core.ports()), self.refreshed)

    def refreshed(self, result):
        self.available, self.devices = result
        self.release["values"] = [r["tag_name"] for r in self.available]
        self.port["values"] = [f"{port} — {name}" for port, name in self.devices]
        if self.devices:
            self.port.current(0)
        if self.available:
            stable = next((i for i, r in enumerate(self.available) if not r["prerelease"]), 0)
            self.release.current(stable)
            self.select_release()
        else:
            self.status.set("No installer-compatible releases yet. Older releases cannot be installed with this tool.")

    def select_release(self, event=None):
        self.selected = self.available[self.release.current()]
        self.data = None
        self.task(lambda: core.manifest(self.selected), self.manifest_loaded)

    def manifest_loaded(self, data):
        if not update.compatible(data) and update.VERSION != "0.0.0":
            self.data = None
            self.status.set("This firmware release requires Music Controller Setup " +
                            update.minimum_version(data) + " or newer. Update the installer.")
            messagebox.showwarning("Installer update required", self.status.get())
            return
        self.data = data
        self.profile["values"] = [p["name"] for p in data["profiles"]]
        self.profile.current(0)
        self.status.set("Ready. Update preserves settings; new-device setup erases all device data.")

    def export(self):
        token = self.settings["device_token"].get()
        if not token:
            return messagebox.showerror("Gateway key", "Enter an existing key or generate a new one first.")
        path = filedialog.asksaveasfilename(initialfile="controller-gateway.env", defaultextension=".env")
        if path:
            try:
                core.export_gateway_key(token, path)
                messagebox.showinfo("Gateway configuration", "Private configuration exported. Set CONTROLLER_DEVICE_TOKEN on your gateway and redeploy. This does not configure the gateway automatically.")
            except Exception:
                messagebox.showerror("Export failed", "Choose a new file path; existing files are never overwritten.")

    def install(self, prepare_only):
        if not self.data or self.profile.current() < 0:
            return
        selected = self.selected
        profile = self.data["profiles"][self.profile.current()]
        full = self.mode.get() == "Set up new device"
        settings = {key: value.get() for key, value in self.settings.items()} if full else None
        try:
            core.validate_profile(profile)
            if full:
                core.validate_settings(settings)
        except ValueError as error:
            return messagebox.showerror("Check settings", str(error))
        destination = None
        port = None
        if prepare_only:
            folder = filedialog.askdirectory(title="Select parent folder for prepared package")
            if not folder:
                return
            destination = Path(folder) / f"music-controller-{selected['tag_name']}-{profile['id']}"
            if destination.exists():
                return messagebox.showerror("Folder exists", "Choose another parent folder to avoid overwriting files.")
            if full and not messagebox.askokcancel("Private files", "The prepared NVS file contains your Wi-Fi password and gateway key. Keep this folder private."):
                return
        else:
            if self.port.current() < 0:
                return messagebox.showerror("USB port", "Connect your device and refresh USB ports.")
            port = self.devices[self.port.current()][0]
            action = "ERASE ALL DATA and provision" if full else "Update firmware and preserve settings"
            if not messagebox.askokcancel("Confirm installation", f"{selected['tag_name']}\n{profile['name']}\n{port}\n\n{action}? Keep USB connected until complete."):
                return
        def work():
            with tempfile.TemporaryDirectory(prefix="music-controller-") as temp:
                target = destination or Path(temp)
                if destination:
                    target.mkdir(exist_ok=False)
                core.prepare(selected, profile, target, lambda message: self.events.put((None, message)))
                if full:
                    core.provision(settings, profile, target)
                if not prepare_only:
                    self.events.put((None, "Flashing device; keep USB connected…"))
                    core.flash(profile, target, port, full)
            return f"Prepared: {destination}. Keep NVS private." if prepare_only else "Installation complete. Temporary files removed."
        self.task(work, lambda result: self.status.set(result))

    def close(self):
        if self.busy:
            messagebox.showinfo("Operation in progress", "Wait for the current operation to finish before closing.")
            return
        self.root.destroy()


def run():
    root = tk.Tk()
    Installer(root)
    root.mainloop()
