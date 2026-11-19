from types import SimpleNamespace
from unittest.mock import MagicMock

import pytest

import simulator as S
from conftest import mqtt_msg, published

LIGHT_NODE, LIGHT_DEV = "node-light", "dev-light"
LIGHT_CMD = f"hesta/nodes/{LIGHT_NODE}/devices/{LIGHT_DEV}/command"
LIGHT_ACK = f"hesta/nodes/{LIGHT_NODE}/devices/{LIGHT_DEV}/ack"
LIGHT_STATE = f"hesta/nodes/{LIGHT_NODE}/devices/{LIGHT_DEV}/state"


class FakeTimer:
    """Replaces threading.Timer so gate travel finishes on demand instead of after 2.5s."""

    def __init__(self, interval, fn):
        self.interval, self.fn = interval, fn
        self.started = self.cancelled = self.fired = False
        self.daemon = False

    def start(self):
        self.started = True

    def is_alive(self):
        return self.started and not self.cancelled and not self.fired

    def cancel(self):
        self.cancelled = True

    def fire(self):
        self.fired = True
        self.fn()


@pytest.fixture
def sim(monkeypatch):
    monkeypatch.setattr(S.threading, "Timer", FakeTimer)
    return S.HardwareSimulator(auto_sensors=False)


def send(sim, topic, payload):
    sim.on_message(None, None, mqtt_msg(topic, payload))


def acks(sim, topic):
    return [p for _, p in published(sim.client, topic)]


# ---------------------------------------------------------------- routing
class TestRouting:
    def test_ac_topic_goes_to_ac(self, sim):
        send(sim, S.AC_COMMAND_TOPIC, {"action": "POWER_OFF"})
        assert sim.ac_state["power"] is False

    def test_other_device_on_ac_node_still_goes_to_ac(self, sim):
        send(sim, f"hesta/nodes/{S.AC_NODE_ID}/devices/other/command", {"action": "TEMP_UP"})
        assert sim.ac_state["temperature"] == 25

    def test_gate_topic_goes_to_gate(self, sim):
        send(sim, S.GATE_COMMAND_TOPIC, {"action": "OPEN"})
        assert sim.gate_state == "OPENING"

    def test_unknown_node_goes_to_light_keyed_by_device(self, sim):
        send(sim, LIGHT_CMD, {"action": "POWER_ON"})
        assert sim.light_states[LIGHT_DEV]["power"] == "ON"

    def test_plain_text_payload_becomes_action(self, sim):
        send(sim, S.GATE_COMMAND_TOPIC, " OPEN ")
        assert sim.gate_state == "OPENING"

    def test_empty_payload_does_not_crash(self, sim):
        send(sim, LIGHT_CMD, b"")
        assert published(sim.client, LIGHT_STATE)

    def test_short_topic_uses_unknown_ids(self, sim):
        send(sim, "garbage", {"action": "POWER_ON"})
        assert sim.light_states["unknown"]["power"] == "ON"

    @pytest.mark.xfail(strict=True, reason="BUG: JSON payload that is not an object (e.g. [1]) raises AttributeError")
    def test_non_object_json_payload(self, sim):
        send(sim, LIGHT_CMD, "[1]")


# ---------------------------------------------------------------- connect / lifecycle
class TestLifecycle:
    def test_on_connect_success_subscribes_and_announces(self, sim):
        client = MagicMock()
        sim.on_connect(client, None, None, 0)
        topics = [c.args[0] for c in client.subscribe.call_args_list]
        assert S.AC_COMMAND_TOPIC in topics and S.GATE_COMMAND_TOPIC in topics
        assert "hesta/nodes/+/devices/+/command" in topics
        status = {t: p for t, p in published(sim.client) if t.endswith("/status")}
        assert status == {S.AC_STATUS_TOPIC: "ONLINE", S.GATE_STATUS_TOPIC: "ONLINE", S.SENSOR_STATUS_TOPIC: "ONLINE"}
        assert published(sim.client, S.AC_TELEMETRY_TOPIC)
        assert published(sim.client, S.GATE_STATE_TOPIC)
        assert published(sim.client, S.DHT22_TOPIC)

    def test_on_connect_accepts_paho_v2_reason_code(self, sim):
        client = MagicMock()
        sim.on_connect(client, None, None, SimpleNamespace(value=0))
        assert client.subscribe.called

    def test_on_connect_failure_does_nothing(self, sim):
        client = MagicMock()
        sim.on_connect(client, None, None, 5)
        assert not client.subscribe.called
        assert not sim.client.publish.called

    def test_start_exits_when_broker_unreachable(self, sim):
        sim.client.connect.side_effect = ConnectionRefusedError()
        with pytest.raises(SystemExit):
            sim.start()

    def test_start_without_auto_sensors(self, sim):
        sim.start()
        sim.client.connect.assert_called_once_with("localhost", 1883, keepalive=60)
        sim.client.loop_start.assert_called_once()
        assert not hasattr(sim, "sensor_thread")

    def test_credentials_are_set(self):
        s = S.HardwareSimulator(username="u", password="p", auto_sensors=False)
        s.client.username_pw_set.assert_called_once_with("u", "p")

    def test_stop(self, sim):
        sim.stop()
        assert sim.running is False
        sim.client.loop_stop.assert_called_once()
        sim.client.disconnect.assert_called_once()


# ---------------------------------------------------------------- AC
class TestAC:
    def cmd(self, sim, **payload):
        send(sim, S.AC_COMMAND_TOPIC, payload)

    def test_power_off_sends_ack_and_telemetry(self, sim):
        self.cmd(sim, action="POWER_OFF", commandId="c1")
        [ack] = acks(sim, S.AC_ACK_TOPIC)
        assert ack["commandId"] == "c1" and ack["status"] == "SUCCESS"
        assert ack["deviceId"] == S.AC_DEVICE_ID and ack["state"]["power"] is False
        [(_, tele)] = published(sim.client, S.AC_TELEMETRY_TOPIC)
        assert tele["power"] is False and tele["deviceType"] == "AIR_CONDITIONER"

    def test_telemetry_is_retained(self, sim):
        self.cmd(sim, action="TEMP_UP")
        call = next(c for c in sim.client.publish.call_args_list if c.args[0] == S.AC_TELEMETRY_TOPIC)
        assert call.kwargs["retain"] is True

    def test_no_command_id_means_no_ack(self, sim):
        self.cmd(sim, action="POWER_ON")
        assert acks(sim, S.AC_ACK_TOPIC) == []
        assert published(sim.client, S.AC_TELEMETRY_TOPIC)

    @pytest.mark.parametrize("payload, expected", [
        ({"action": "POWER_ON"}, True),
        ({"action": "TURN_OFF"}, False),
        ({"action": "SET_POWER", "power": False}, False),
        ({"action": "SET_POWER", "power": "on"}, True),
        ({"action": "SET_POWER", "power": "0"}, False),
        ({"action": "SET_POWER", "parameters": {"power": "TRUE"}}, True),
        ({"action": "POWER_ON", "power": False}, False),  # explicit power field wins over action name
    ])
    def test_power(self, sim, payload, expected):
        sim.ac_state["power"] = not expected
        send(sim, S.AC_COMMAND_TOPIC, payload)
        assert sim.ac_state["power"] is expected

    @pytest.mark.parametrize("payload, expected", [
        ({"temperature": 20}, 20),
        ({"temperature": 35}, 30),
        ({"temperature": 5}, 16),
        ({"temperature": "22"}, 22),
        ({"temp": 18}, 18),
        ({"parameters": {"temperature": 26}}, 26),
        ({}, 24),  # missing value keeps current
    ])
    def test_set_temperature(self, sim, payload, expected):
        send(sim, S.AC_COMMAND_TOPIC, {"action": "SET_TEMPERATURE", **payload})
        assert sim.ac_state["temperature"] == expected

    @pytest.mark.xfail(strict=True, reason="BUG: non-numeric temperature raises ValueError inside the MQTT callback")
    def test_set_temperature_invalid(self, sim):
        self.cmd(sim, action="SET_TEMPERATURE", temperature="hot")

    @pytest.mark.parametrize("action, start, expected", [
        ("TEMP_UP", 24, 25), ("TEMPERATURE_PLUS", 30, 30),
        ("TEMP_DOWN", 24, 23), ("TEMPERATURE_MINUS", 16, 16),
    ])
    def test_temp_step_clamped(self, sim, action, start, expected):
        sim.ac_state["temperature"] = start
        self.cmd(sim, action=action)
        assert sim.ac_state["temperature"] == expected

    def test_mode_and_fan_uppercased(self, sim):
        self.cmd(sim, action="SET_MODE", mode="heat")
        self.cmd(sim, action="SET_FAN", parameters={"fan": "high"})
        assert sim.ac_state["mode"] == "HEAT" and sim.ac_state["fan"] == "HIGH"

    def test_mode_and_fan_defaults(self, sim):
        sim.ac_state.update(mode="HEAT", fan="LOW")
        self.cmd(sim, action="SET_MODE")
        self.cmd(sim, action="SET_FAN")
        assert sim.ac_state["mode"] == "COOL" and sim.ac_state["fan"] == "AUTO"

    @pytest.mark.parametrize("payload, expected", [
        ({"swing": True}, True), ({"swing": "on"}, True), ({"swing": "false"}, False),
        ({"parameters": {"swing": True}}, True), ({}, False),
    ])
    def test_swing(self, sim, payload, expected):
        send(sim, S.AC_COMMAND_TOPIC, {"action": "SET_SWING", **payload})
        assert sim.ac_state["swing"] is expected

    def test_timer_set_and_cancel(self, sim):
        self.cmd(sim, action="SET_TIMER", hour=3, halfHour=True)
        assert (sim.ac_state["timerEnabled"], sim.ac_state["timerHour"], sim.ac_state["timerHalfHour"]) == (True, 3, True)
        self.cmd(sim, action="CANCEL_TIMER")
        assert (sim.ac_state["timerEnabled"], sim.ac_state["timerHour"], sim.ac_state["timerHalfHour"]) == (False, 0, False)

    def test_timer_defaults_to_one_hour(self, sim):
        self.cmd(sim, action="SET_TIMER")
        assert sim.ac_state["timerHour"] == 1 and sim.ac_state["timerHalfHour"] is False

    def test_lowercase_action(self, sim):
        self.cmd(sim, action="power_off")
        assert sim.ac_state["power"] is False

    def test_unknown_action_keeps_state(self, sim):
        before = dict(sim.ac_state)
        self.cmd(sim, action="SELF_DESTRUCT")
        assert sim.ac_state == before


# ---------------------------------------------------------------- Gate
class TestGate:
    def cmd(self, sim, **payload):
        send(sim, S.GATE_COMMAND_TOPIC, payload)

    def states(self, sim):
        return [p for _, p in published(sim.client, S.GATE_STATE_TOPIC)]

    def test_open_full_cycle(self, sim):
        self.cmd(sim, action="OPEN", commandId="g1")
        assert sim.gate_state == "OPENING"
        [ack] = acks(sim, S.GATE_ACK_TOPIC)
        assert (ack["commandId"], ack["status"], ack["deviceId"], ack["state"]) == (
            "g1", "SUCCESS", S.GATE_DEVICE_ID, {"state": "OPENING"})
        last = self.states(sim)[-1]
        assert (last["state"], last["limit_open"], last["limit_close"]) == ("OPENING", False, False)
        assert sim.gate_timer.interval == 2.5
        sim.gate_timer.fire()
        assert sim.gate_state == "OPEN"
        last = self.states(sim)[-1]
        assert (last["state"], last["limit_open"], last["limit_close"]) == ("OPEN", True, False)

    def test_close_full_cycle(self, sim):
        sim.gate_state = "OPEN"
        self.cmd(sim, action="GATE_CLOSE")
        assert sim.gate_state == "CLOSING"
        sim.gate_timer.fire()
        last = self.states(sim)[-1]
        assert (last["state"], last["limit_open"], last["limit_close"]) == ("CLOSED", False, True)

    def test_stop_while_opening_cancels_travel(self, sim):
        self.cmd(sim, action="OPEN")
        timer = sim.gate_timer
        self.cmd(sim, action="STOP", commandId="s1")
        assert timer.cancelled and sim.gate_state == "STOPPED"
        assert acks(sim, S.GATE_ACK_TOPIC)[-1]["state"] == {"state": "STOPPED"}
        timer.fire()  # timer that already fired past cancel must not move the gate
        assert sim.gate_state == "STOPPED"

    def test_reverse_direction_mid_travel(self, sim):
        self.cmd(sim, action="OPEN")
        open_timer = sim.gate_timer
        self.cmd(sim, action="close")
        assert open_timer.cancelled and sim.gate_state == "CLOSING"
        open_timer.fire()
        assert sim.gate_state == "CLOSING"
        sim.gate_timer.fire()
        assert sim.gate_state == "CLOSED"

    def test_no_command_id_means_no_ack(self, sim):
        self.cmd(sim, action="OPEN")
        assert acks(sim, S.GATE_ACK_TOPIC) == []

    def test_unknown_action_keeps_state(self, sim):
        self.cmd(sim, action="FLY", commandId="x")
        assert sim.gate_state == "CLOSED"
        assert published(sim.client) == []

    @pytest.mark.parametrize("state, lo, lc", [("OPEN", True, False), ("CLOSED", False, True), ("STOPPED", False, False)])
    def test_state_limits_derived_from_state(self, sim, state, lo, lc):
        sim.gate_state = state
        sim.publish_gate_state()
        last = self.states(sim)[-1]
        assert (last["limit_open"], last["limit_close"]) == (lo, lc)


# ---------------------------------------------------------------- Lights / LED
class TestLight:
    def cmd(self, sim, **payload):
        send(sim, LIGHT_CMD, payload)

    @property
    def default(self):
        return {"power": "OFF", "brightness": 80, "r": 255, "g": 255, "b": 255}

    def state(self, sim):
        return sim.light_states[LIGHT_DEV]

    def test_default_state_created(self, sim):
        self.cmd(sim, action="NOOP")
        assert self.state(sim).items() >= self.default.items()

    @pytest.mark.parametrize("payload, expected", [
        ({"action": "POWER_ON"}, "ON"), ({"action": "TURN_ON"}, "ON"),
        ({"action": "POWER_OFF"}, "OFF"), ({"action": "TURN_OFF"}, "OFF"),
        ({"action": "SET_POWER", "power": "false"}, "OFF"),
        ({"action": "SET_POWER", "parameters": {"power": 1}}, "ON"),
        ({"action": "SET_POWER"}, "ON"),
    ])
    def test_power(self, sim, payload, expected):
        send(sim, LIGHT_CMD, payload)
        assert self.state(sim)["power"] == expected

    @pytest.mark.parametrize("value, brightness, power", [
        (50, 50, "ON"), (150, 100, "ON"), (-5, 0, "OFF"), (0, 0, "OFF"), ("abc", 80, "OFF"),
    ])
    def test_brightness(self, sim, value, brightness, power):
        self.cmd(sim, action="SET_BRIGHTNESS", brightness=value)
        assert (self.state(sim)["brightness"], self.state(sim)["power"]) == (brightness, power)

    def test_brightness_from_parameters(self, sim):
        self.cmd(sim, action="SET_BRIGHTNESS", parameters={"brightness": 30})
        assert self.state(sim)["brightness"] == 30

    def test_rgb_clamped_and_turns_on(self, sim):
        self.cmd(sim, action="SET_RGB", r=300, g=-1, b="10")
        s = self.state(sim)
        assert (s["r"], s["g"], s["b"], s["power"]) == (255, 0, 10, "ON")
        assert s["color"] == {"r": 255, "g": 0, "b": 10}

    def test_set_color_alias_with_brightness_in_parameters(self, sim):
        self.cmd(sim, action="SET_COLOR", parameters={"r": 1, "g": 2, "b": 3, "brightness": 40})
        s = self.state(sim)
        assert (s["r"], s["g"], s["b"], s["brightness"]) == (1, 2, 3, 40)

    def test_rgb_invalid_value_keeps_color(self, sim):
        self.cmd(sim, action="SET_RGB", r="red", g=0, b=0)
        assert (self.state(sim)["r"], self.state(sim)["power"]) == (255, "OFF")

    def test_partial_rgb_keeps_other_channels(self, sim):
        self.cmd(sim, action="SET_RGB", r=10, g=20, b=30)
        self.cmd(sim, action="SET_RGB", g=99)
        s = self.state(sim)
        assert (s["r"], s["g"], s["b"]) == (10, 99, 30)

    def test_ack_and_state_topics(self, sim):
        self.cmd(sim, action="POWER_ON", commandId="l1")
        [ack] = acks(sim, LIGHT_ACK)
        assert ack["commandId"] == "l1" and ack["deviceId"] == LIGHT_DEV and ack["state"]["power"] == "ON"
        [(_, state)] = published(sim.client, LIGHT_STATE)
        assert state["deviceId"] == LIGHT_DEV and state["power"] == "ON"

    def test_devices_are_independent(self, sim):
        self.cmd(sim, action="POWER_ON")
        send(sim, "hesta/nodes/n/devices/other/command", {"action": "SET_BRIGHTNESS", "brightness": 10})
        assert self.state(sim)["brightness"] == 80
        assert sim.light_states["other"]["power"] == "ON"

    def test_non_dict_parameters_ignored(self, sim):
        self.cmd(sim, action="SET_BRIGHTNESS", parameters="nope", brightness=10)
        assert self.state(sim)["brightness"] == 10


# ---------------------------------------------------------------- Sensors
class TestSensors:
    def test_publish_all_sensors(self, sim):
        sim.publish_all_sensors()
        msgs = published(sim.client)
        assert [(t, p["metricType"]) for t, p in msgs] == [
            (S.DHT22_TOPIC, "temperature"), (S.DHT22_TOPIC, "humidity"),
            (S.LDR_TOPIC, "LIGHT_SENSOR"), (S.PIR_TOPIC, "MOTION_SENSOR"),
        ]
        temp = msgs[0][1]
        assert abs(temp["value"] - 27.5) <= 0.31 and temp["unit"] == "°C" and temp["nodeId"] == S.SENSOR_NODE_ID

    @pytest.mark.parametrize("lux, expected", [(600.0, 1), (150.0, 0)])
    def test_ldr_threshold(self, sim, lux, expected):
        sim.sensor_data["lux"] = lux
        sim.publish_all_sensors()
        [(_, p)] = published(sim.client, S.LDR_TOPIC)
        assert p["value"] == expected

    def test_motion_detected_then_auto_clears(self, sim, monkeypatch):
        monkeypatch.setattr(S.time, "sleep", lambda s: None)
        monkeypatch.setattr(S.threading, "Thread", lambda target, daemon: SimpleNamespace(start=target))
        sim.trigger_motion(True)
        assert [p["value"] for _, p in published(sim.client, S.PIR_TOPIC)] == [1, 0]
        assert sim.sensor_data["motion"] == 0

    def test_motion_cleared_manually(self, sim):
        sim.sensor_data["motion"] = 1
        sim.trigger_motion(False)
        assert [p["value"] for _, p in published(sim.client, S.PIR_TOPIC)] == [0]
        assert sim.sensor_data["motion"] == 0

    def test_status_is_retained_json_string(self, sim):
        sim.publish_status("n1", "OFFLINE")
        sim.client.publish.assert_called_once_with("hesta/nodes/n1/status", '"OFFLINE"', qos=1, retain=True)


# ---------------------------------------------------------------- CLI
class TestCli:
    def run(self, monkeypatch, argv, keys):
        fake = MagicMock()
        fake.return_value.sensor_data = {"temperature": 27.0, "lux": 450.0}
        monkeypatch.setattr(S, "HardwareSimulator", fake)
        monkeypatch.setattr(S.sys, "argv", ["simulator.py", *argv])
        inputs = iter(keys)
        monkeypatch.setattr("builtins.input", lambda: next(inputs))
        S.main()
        return fake

    def test_args_and_keys(self, monkeypatch):
        fake = self.run(monkeypatch, ["--broker", "1.2.3.4", "--port", "1999", "--no-auto-sensors"],
                        ["m", "t", "t", "d", "l", "s", "q"])
        fake.assert_called_once_with(broker="1.2.3.4", port=1999, username=None, password=None, auto_sensors=False)
        sim = fake.return_value
        sim.trigger_motion.assert_called_once_with(detected=True)
        assert sim.sensor_data == {"temperature": 28.0, "lux": 150.0}  # 450 lux is "bright", so 'l' dims it
        assert sim.publish_all_sensors.call_count == 5
        sim.stop.assert_called_once()

    def test_env_defaults(self, monkeypatch):
        monkeypatch.setenv("MQTT_BROKER_HOST", "broker.lan")
        monkeypatch.setenv("MQTT_USERNAME", "u")
        monkeypatch.setenv("MQTT_PASSWORD", "p")
        fake = self.run(monkeypatch, [], ["q"])
        fake.assert_called_once_with(broker="broker.lan", port=1883, username="u", password="p", auto_sensors=True)

    def test_eof_stops_cleanly(self, monkeypatch):
        fake = MagicMock()
        monkeypatch.setattr(S, "HardwareSimulator", fake)
        monkeypatch.setattr(S.sys, "argv", ["simulator.py"])
        monkeypatch.setattr("builtins.input", MagicMock(side_effect=EOFError))
        S.main()
        fake.return_value.stop.assert_called_once()
