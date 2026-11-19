import asyncio
from unittest.mock import AsyncMock, MagicMock

import pytest

from conftest import mqtt_msg, published

# led.py connects BLE + MQTT at import time; conftest stubs both so this import is safe.
import led as L

NODE, DEV = "node-1", "led-1"
NEW_ACK = f"hesta/nodes/{NODE}/devices/{DEV}/ack"
NEW_STATE = f"hesta/nodes/{NODE}/devices/{DEV}/state"
OLD_ACK = f"hesta/device/{DEV}/ack"
OLD_STATE = f"hesta/device/{DEV}/state"


@pytest.fixture
def ble(monkeypatch, fake_ble_cls):
    fake = fake_ble_cls("test")
    monkeypatch.setattr(L, "led", fake)
    return fake


@pytest.fixture
def client():
    return MagicMock()


def run(action, params=None, command_id="c1", client=None):
    asyncio.run(L.execute_command(action, params or {}, command_id, NODE, DEV, client))


def test_import_connected_ble_and_mqtt():
    L.client.connect.assert_called_once_with(L.MQTT_BROKER, L.MQTT_PORT, 60)
    L.client.loop_forever.assert_called_once()


class TestExecuteCommand:
    @pytest.mark.parametrize("action, method, power", [
        ("POWER_ON", "power_on", "ON"), ("TURN_ON", "power_on", "ON"),
        ("POWER_OFF", "power_off", "OFF"), ("TURN_OFF", "power_off", "OFF"),
    ])
    def test_power(self, ble, client, action, method, power):
        run(action, client=client)
        getattr(ble, method).assert_awaited_once()
        [(_, ack)] = published(client, NEW_ACK)
        assert ack == {"commandId": "c1", "deviceId": DEV, "status": "SUCCESS", "errorCode": None,
                       "message": None, "state": {"power": power}}
        assert published(client, OLD_ACK) == [(OLD_ACK, ack)]
        assert published(client, NEW_STATE)[0][1] == {"deviceId": DEV, "state": {"power": power}}
        assert published(client, OLD_STATE)

    def test_set_rgb(self, ble, client):
        run("SET_RGB", {"r": "10", "g": 20}, client=client)
        ble.set_rgb.assert_awaited_once_with(10, 20, 255)
        assert published(client, NEW_STATE)[0][1]["state"] == {"power": "ON", "r": 10, "g": 20, "b": 255}

    @pytest.mark.parametrize("value, state", [(50, {"brightness": 50, "power": "ON"}), (0, {"brightness": 0})])
    def test_set_brightness(self, ble, client, value, state):
        run("SET_BRIGHTNESS", {"brightness": value}, client=client)
        ble.set_brightness.assert_awaited_once_with(value)
        assert published(client, NEW_ACK)[0][1]["state"] == state

    def test_brightness_default_100(self, ble, client):
        run("SET_BRIGHTNESS", client=client)
        ble.set_brightness.assert_awaited_once_with(100)

    def test_unknown_action_fails_without_state(self, ble, client):
        run("DANCE", client=client)
        [(_, ack)] = published(client, NEW_ACK)
        assert ack["status"] == "FAILED" and ack["errorCode"] == "EXECUTION_ERROR"
        assert ack["message"] == "Unsupported action: DANCE" and ack["state"] is None
        assert published(client, NEW_STATE) == []

    def test_ble_error_reported_as_failed(self, ble, client):
        ble.power_on.side_effect = OSError("BLE disconnected")
        run("POWER_ON", client=client)
        ack = published(client, NEW_ACK)[0][1]
        assert ack["status"] == "FAILED" and ack["message"] == "BLE disconnected"
        assert published(client, NEW_STATE) == []

    def test_invalid_number_reported_as_failed(self, ble, client):
        run("SET_BRIGHTNESS", {"brightness": "abc"}, client=client)
        assert published(client, NEW_ACK)[0][1]["status"] == "FAILED"
        ble.set_brightness.assert_not_awaited()

    def test_no_command_id_skips_ack_but_sends_state(self, ble, client):
        run("POWER_ON", command_id=None, client=client)
        assert published(client, NEW_ACK) == []
        assert published(client, NEW_STATE)


class TestOnMessage:
    @pytest.fixture
    def exe(self, monkeypatch):
        mock = AsyncMock()
        monkeypatch.setattr(L, "execute_command", mock)
        return mock

    def send(self, topic, payload, client):
        L.on_message(client, None, mqtt_msg(topic, payload))

    def test_new_topic_format(self, exe, client):
        self.send(f"hesta/nodes/{NODE}/devices/{DEV}/command",
                  {"action": "SET_RGB", "commandId": "c9", "parameters": {"r": 1}}, client)
        exe.assert_called_once_with("SET_RGB", {"r": 1}, "c9", NODE, DEV, client)

    def test_legacy_topic_format(self, exe, client):
        self.send("hesta/device/strip-7/command", {"action": "POWER_ON"}, client)
        exe.assert_called_once_with("POWER_ON", {}, None, "unknown", "strip-7", client)

    def test_unrecognised_topic_uses_default_device(self, exe, client):
        self.send("other/topic", {"action": "POWER_ON"}, client)
        assert exe.call_args.args[3:5] == ("unknown", "led-strip-01")

    def test_payload_target_overrides_topic_device(self, exe, client):
        self.send(f"hesta/nodes/{NODE}/devices/{DEV}/command", {"action": "POWER_ON", "target": "x"}, client)
        assert exe.call_args.args[4] == "x"

    def test_root_fields_merged_but_parameters_win(self, exe, client):
        self.send("hesta/device/d/command",
                  {"action": "SET_RGB", "r": 1, "g": 2, "brightness": 5, "parameters": {"r": 9}}, client)
        assert exe.call_args.args[1] == {"r": 9, "g": 2, "brightness": 5}

    def test_non_dict_parameters_ignored(self, exe, client):
        self.send("hesta/device/d/command", {"action": "SET_RGB", "parameters": [1], "b": 3}, client)
        assert exe.call_args.args[1] == {"b": 3}

    def test_invalid_json_is_dropped(self, exe, client):
        self.send("hesta/device/d/command", "not json", client)
        exe.assert_not_called()


def test_on_connect_subscribes_all_topics(client):
    L.on_connect(client, None, None, 0)
    client.subscribe.assert_called_once_with(L.MQTT_TOPICS)


def test_on_connect_failure(client):
    L.on_connect(client, None, None, 5)
    client.subscribe.assert_not_called()
