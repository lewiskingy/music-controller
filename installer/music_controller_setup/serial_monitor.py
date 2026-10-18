"""Read-only serial diagnostics with explicit, exclusive COM-port ownership."""
import threading
import time


class SerialMonitor:
    """Background reader. Callbacks execute on worker thread, never touch Tk."""
    def __init__(self, on_line, baudrate=115200):
        self.on_line = on_line
        self.baudrate = baudrate
        self.port = None
        self._stop = threading.Event()
        self._thread = None
        self._serial = None

    @property
    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def start(self, port):
        if self.running:
            return False
        self.port = port
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()
        return True

    def stop(self):
        """Wait for the reader to release COM before returning to esptool."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=3)
            if self._thread.is_alive():
                raise RuntimeError("Serial monitor did not release the USB port")
        self._thread = None
        self._serial = None

    def _read_loop(self):
        import serial
        buffer = bytearray()
        while not self._stop.is_set():
            connection = None
            try:
                # Explicitly prevent serial-control-line toggles resetting the board.
                connection = serial.Serial(port=None, baudrate=self.baudrate,
                                           timeout=0.25, write_timeout=0.25)
                connection.dtr = False
                connection.rts = False
                connection.port = self.port
                connection.open()
                self._serial = connection
                self.on_line(f"[Connected to {self.port} at {self.baudrate} baud]")
                while not self._stop.is_set():
                    data = connection.read(512)
                    if not data:
                        continue
                    buffer.extend(data)
                    if len(buffer) > 8192:
                        buffer = buffer[-8192:]
                    while b"\n" in buffer:
                        line, _, rest = buffer.partition(b"\n")
                        buffer = bytearray(rest)
                        self.on_line(line.decode("utf-8", errors="replace").rstrip("\r"))
            except (serial.SerialException, OSError) as error:
                if not self._stop.is_set():
                    self.on_line(f"[Disconnected: {type(error).__name__}; retrying]")
                    self._stop.wait(1)
            finally:
                self._serial = None
                if connection is not None:
                    try:
                        connection.close()
                    except OSError:
                        pass
        self.on_line("[Monitoring stopped]")
