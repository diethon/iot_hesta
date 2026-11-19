"""Contract the backend relies on: every ACK, whoever sends it, has these fields."""
import asyncio
from unittest.mock import MagicMock

import pytest

import led as L
import simulator as S
from conftest import mqtt_msg, published

REQUIRED_ACK = {"commandId", "deviceId", "status", "state"}


def sim_ack(topic, ack_topic, action):
    sim = S.HardwareSimulator(auto_sensors=False)
    sim.on_message(None, None, mqtt_msg(topic, {"action": action, "commandId": "k"}))
    return published(sim.client, ack_topic)[0][1]


def led_ack(action, monkeypatch, fake_ble_cls):
    monkeypatch.setattr(L, "led", fake_ble_cls())
    client = MagicMock()
    asyncio.run(L.execute_command(action, {}, "k", "n", "d", client))
    return published(client, "hesta/nodes/n/devices/d/ack")[0][1]


@pytest.fixture(params=["ac", "gate", "light", "led-ok", "led-fail"])
def ack(request, monkeypatch, fake_ble_cls):
    monkeypatch.setattr(S.threading, "Timer", MagicMock())
    return {
        "ac": lambda: sim_ack(S.AC_COMMAND_TOPIC, S.AC_ACK_TOPIC, "POWER_ON"),
        "gate": lambda: sim_ack(S.GATE_COMMAND_TOPIC, S.GATE_ACK_TOPIC, "OPEN"),
        "light": lambda: sim_ack("hesta/nodes/n/devices/d/command", "hesta/nodes/n/devices/d/ack", "POWER_ON"),
        "led-ok": lambda: led_ack("POWER_ON", monkeypatch, fake_ble_cls),
        "led-fail": lambda: led_ack("NOPE", monkeypatch, fake_ble_cls),
    }[request.param]()


def test_ack_has_required_fields(ack):
    assert REQUIRED_ACK <= ack.keys()
    assert ack["commandId"] == "k"
    assert ack["status"] in ("SUCCESS", "FAILED")
