"""Serial monitor does not retain the COM port across flash handoff."""
import sys
import time
from unittest.mock import patch
from music_controller_setup.serial_monitor import SerialMonitor


def test_stop_releases_port_before_flash():
    events = []
    connections = []
    class FakeSerial:
        def __init__(self, **kwargs):
            self.is_open = False
            self.port = None
            self.dtr = None
            self.rts = None
            connections.append(self)
        def open(self):
            self.is_open = True
        def read(self, size):
            time.sleep(.01)
            return b"I controller: ready\n"
        def close(self):
            self.is_open = False

    with patch.dict(sys.modules, {"serial": type("Serial", (), {"Serial": FakeSerial})()}):
        monitor = SerialMonitor(events.append)
        assert monitor.start("COM4")
        time.sleep(.04)
        monitor.stop()
        assert connections and all(not c.is_open for c in connections)
        assert connections[0].dtr is False and connections[0].rts is False
        assert any("ready" in line for line in events)
        assert not monitor.running
