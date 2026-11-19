import json
import pathlib
import sys
import types
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT), str(ROOT / "Device" / "LED")]

# Fake paho-mqtt: unit tests must never touch a real broker (and paho may not be installed).
_client_mod = types.ModuleType("paho.mqtt.client")
_client_mod.Client = MagicMock(side_effect=lambda *a, **k: MagicMock())
_client_mod.CallbackAPIVersion = SimpleNamespace(VERSION2=2)
_mqtt_mod = types.ModuleType("paho.mqtt")
_mqtt_mod.client = _client_mod
_paho_mod = types.ModuleType("paho")
_paho_mod.mqtt = _mqtt_mod
sys.modules.update({"paho": _paho_mod, "paho.mqtt": _mqtt_mod, "paho.mqtt.client": _client_mod})


# Fake ble_led: led.py imports it but it is not in the repo; real BLE is hardware-only.
class FakeBleLed:
    def __init__(self, address=None):
        self.address = address
        for name in ("connect", "power_on", "power_off", "set_rgb", "set_brightness"):
            setattr(self, name, AsyncMock())


_ble_mod = types.ModuleType("ble_led")
_ble_mod.BleLedController = FakeBleLed
sys.modules["ble_led"] = _ble_mod


def published(client, topic=None):
    """(topic, decoded payload) of every client.publish call, optionally filtered by topic."""
    out = []
    for c in client.publish.call_args_list:
        t, payload = c.args[0], c.args[1]
        if topic is None or t == topic:
            out.append((t, json.loads(payload)))
    return out


def mqtt_msg(topic, payload):
    if not isinstance(payload, (str, bytes)):
        payload = json.dumps(payload)
    if isinstance(payload, str):
        payload = payload.encode()
    return SimpleNamespace(topic=topic, payload=payload)


@pytest.fixture
def fake_ble_cls():
    return FakeBleLed
