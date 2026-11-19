#!/usr/bin/env python3
"""
HESTA Smart Home - Multi-Node IoT Hardware Simulator
=====================================================
Simulates real physical ESP32 / Arduino hardware nodes communicating over MQTT:
1. Node 1: Casper AC (Điều hòa Casper IR)
   - Node Code: 32442d7f-dfcf-4b2f-a380-adba32876cec
   - Device ID: 05d8a4fd-d7f1-4c62-a70e-a4213552b054
   - Inbound: hesta/nodes/32442d7f-dfcf-4b2f-a380-adba32876cec/devices/05d8a4fd-d7f1-4c62-a70e-a4213552b054/command
   - Outbound Telemetry: .../telemetry
   - Outbound ACK: .../ack
   - Outbound Status: .../status

2. Node 2: Sliding Gate (Cổng trượt tự động motor L298N)
   - Node Code: 8d1cdd82-b339-469e-be13-7e91070f7ae5
   - Device ID: 2d566d0a-f7d6-4fac-a0bf-18912da4ab28
   - Inbound: hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/devices/2d566d0a-f7d6-4fac-a0bf-18912da4ab28/command
   - Outbound State: .../state
   - Outbound ACK: .../ack

3. Node 3: Environment & Security Sensors (DHT22, LDR, PIR)
   - Node Code: 109441d8-b6bf-4a18-bdf8-9279b7a1548d
   - DHT22 Device ID: 0bb7dc6e-0556-413d-a507-66bc8225b4ba (Temperature & Humidity)
   - LDR Device ID:   57c13343-f2cd-45ce-b65d-0bd843ae95d3 (Light / Lux Sensor)
   - PIR Device ID:   5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2 (Motion Sensor)

Usage:
  python simulator.py [--broker BROKER_IP] [--port PORT] [--auto-sensors]
Example:
  python simulator.py --broker localhost --port 1883
"""

import sys
import os
import json
import time
import random
import threading
import argparse
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("[ERROR] Thư viện paho-mqtt chưa được cài đặt. Hãy chạy: pip install paho-mqtt")
    sys.exit(1)

# ==============================================================================
# Hardware Identifiers & Topic Definitions
# ==============================================================================
# Node 1: Casper AC
AC_NODE_ID = "32442d7f-dfcf-4b2f-a380-adba32876cec"
AC_DEVICE_ID = "05d8a4fd-d7f1-4c62-a70e-a4213552b054"
AC_COMMAND_TOPIC = f"hesta/nodes/{AC_NODE_ID}/devices/{AC_DEVICE_ID}/command"
AC_TELEMETRY_TOPIC = f"hesta/nodes/{AC_NODE_ID}/devices/{AC_DEVICE_ID}/telemetry"
AC_ACK_TOPIC = f"hesta/nodes/{AC_NODE_ID}/devices/{AC_DEVICE_ID}/ack"
AC_STATUS_TOPIC = f"hesta/nodes/{AC_NODE_ID}/status"

# Node 2: Sliding Gate L298N
GATE_NODE_ID = "8d1cdd82-b339-469e-be13-7e91070f7ae5"
GATE_DEVICE_ID = "2d566d0a-f7d6-4fac-a0bf-18912da4ab28"
GATE_COMMAND_TOPIC = f"hesta/nodes/{GATE_NODE_ID}/devices/{GATE_DEVICE_ID}/command"
GATE_STATE_TOPIC = f"hesta/nodes/{GATE_NODE_ID}/devices/{GATE_DEVICE_ID}/state"
GATE_ACK_TOPIC = f"hesta/nodes/{GATE_NODE_ID}/devices/{GATE_DEVICE_ID}/ack"
GATE_STATUS_TOPIC = f"hesta/nodes/{GATE_NODE_ID}/status"

# Node 3: Sensors
SENSOR_NODE_ID = "109441d8-b6bf-4a18-bdf8-9279b7a1548d"
DHT22_DEVICE_ID = "0bb7dc6e-0556-413d-a507-66bc8225b4ba"
LDR_DEVICE_ID = "57c13343-f2cd-45ce-b65d-0bd843ae95d3"
PIR_DEVICE_ID = "5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2"

DHT22_TOPIC = f"hesta/nodes/{SENSOR_NODE_ID}/devices/{DHT22_DEVICE_ID}/telemetry"
LDR_TOPIC = f"hesta/nodes/{SENSOR_NODE_ID}/devices/{LDR_DEVICE_ID}/telemetry"
PIR_TOPIC = f"hesta/nodes/{SENSOR_NODE_ID}/devices/{PIR_DEVICE_ID}/telemetry"
SENSOR_STATUS_TOPIC = f"hesta/nodes/{SENSOR_NODE_ID}/status"

# ANSI Colors for Rich Console Output
C_RESET = "\033[0m"
C_BOLD = "\033[1m"
C_CYAN = "\033[96m"
C_GREEN = "\033[92m"
C_YELLOW = "\033[93m"
C_RED = "\033[91m"
C_MAGENTA = "\033[95m"
C_BLUE = "\033[94m"


def timestamp_str():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class HardwareSimulator:
    def __init__(self, broker="localhost", port=1883, username=None, password=None, auto_sensors=True):
        self.broker = broker
        self.port = port
        self.username = username
        self.password = password
        self.auto_sensors = auto_sensors
        self.running = True

        # State: Casper AC
        self.ac_state = {
            "power": True,
            "temperature": 24,
            "mode": "COOL",
            "fan": "AUTO",
            "swing": False,
            "timerEnabled": False,
            "timerHour": 0,
            "timerHalfHour": False,
        }

        # State: Sliding Gate
        self.gate_state = "CLOSED"  # CLOSED, OPENING, OPEN, CLOSING, STOPPED
        self.gate_timer = None
        self.gate_lock = threading.Lock()

        # State: Sensors
        self.sensor_data = {
            "temperature": 27.5,
            "humidity": 62.0,
            "lux": 450.0,
            "motion": 0,
        }

        # Initialize MQTT Client
        client_id = f"hesta-hardware-sim-{random.randint(1000, 9999)}"
        # Support paho-mqtt v1 & v2 API
        try:
            self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id)
        except AttributeError:
            self.client = mqtt.Client(client_id)

        if self.username and self.password:
            self.client.username_pw_set(self.username, self.password)

        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect

    def start(self):
        print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
        print(f"{C_BOLD}{C_CYAN}         HESTA SMART HOME - IOT HARDWARE SIMULATOR                   {C_RESET}")
        print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
        print(f"Connecting to MQTT Broker: {C_YELLOW}{self.broker}:{self.port}{C_RESET}")

        try:
            self.client.connect(self.broker, self.port, keepalive=60)
        except Exception as e:
            print(f"{C_RED}[ERROR] Không thể kết nối tới Broker {self.broker}:{self.port}: {e}{C_RESET}")
            print(f"{C_YELLOW}[GỢI Ý] Hãy kiểm tra Mosquitto Broker đang chạy và thử: python simulator.py --broker localhost{C_RESET}")
            sys.exit(1)

        self.client.loop_start()

        # Start periodic sensor publisher thread
        if self.auto_sensors:
            self.sensor_thread = threading.Thread(target=self._sensor_loop, daemon=True)
            self.sensor_thread.start()

        print(f"{C_GREEN}[SIMULATOR READY]{C_RESET} Tất cả các Node đã online và sẵn sàng nhận lệnh từ Web/App/BE.")

    def on_connect(self, client, userdata, flags, rc, properties=None):
        rc_code = rc if isinstance(rc, int) else getattr(rc, "value", rc)
        if rc_code == 0:
            print(f"{C_GREEN}[✓ MQTT CONNECTED]{C_RESET} Đã kết nối thành công tới broker ({self.broker}:{self.port})")

            # 1. Subscribe to command topics for AC, Gate, and all Smart Lights / LEDs
            client.subscribe(AC_COMMAND_TOPIC, qos=1)
            client.subscribe(f"hesta/nodes/{AC_NODE_ID}/devices/+/command", qos=1)
            client.subscribe(GATE_COMMAND_TOPIC, qos=1)
            client.subscribe(f"hesta/nodes/{GATE_NODE_ID}/devices/+/command", qos=1)
            client.subscribe("hesta/nodes/+/devices/+/command", qos=1)

            print(f"  → Subscribed: {C_CYAN}{AC_COMMAND_TOPIC}{C_RESET}")
            print(f"  → Subscribed: {C_CYAN}{GATE_COMMAND_TOPIC}{C_RESET}")
            print(f"  → Subscribed: {C_CYAN}hesta/nodes/+/devices/+/command (Lights & LEDs){C_RESET}")

            # 2. Publish initial ONLINE status and telemetry
            self.publish_status(AC_NODE_ID, "ONLINE")
            self.publish_status(GATE_NODE_ID, "ONLINE")
            self.publish_status(SENSOR_NODE_ID, "ONLINE")

            self.publish_ac_telemetry()
            self.publish_gate_state()
            self.publish_all_sensors()
        else:
            print(f"{C_RED}[✕ MQTT CONNECT FAILED]{C_RESET} Mã phản hồi: {rc_code}")

    def on_disconnect(self, client, userdata, rc, properties=None):
        if self.running:
            print(f"{C_YELLOW}[!] MQTT Disconnected. Đang thử kết nối lại...{C_RESET}")

    def on_message(self, client, userdata, msg):
        topic = msg.topic
        payload_raw = msg.payload.decode("utf-8", errors="ignore")
        now = timestamp_str()

        print(f"\n{C_BOLD}{C_MAGENTA}──────────────────────────────────────────────────────────────────────{C_RESET}")
        print(f"{C_BOLD}{C_MAGENTA}[INBOUND COMMAND RECEIVED] {now}{C_RESET}")
        print(f"  Topic:   {C_CYAN}{topic}{C_RESET}")
        print(f"  Payload: {payload_raw}")

        try:
            data = json.loads(payload_raw) if payload_raw else {}
        except json.JSONDecodeError:
            data = {"action": payload_raw.strip()}

        # Route message
        parts = topic.split("/")
        node_id = parts[2] if len(parts) >= 3 else "unknown"
        device_id = parts[4] if len(parts) >= 5 else "unknown"

        if AC_NODE_ID in topic or "devices/" + AC_DEVICE_ID in topic:
            self.handle_ac_command(data)
        elif GATE_NODE_ID in topic or "devices/" + GATE_DEVICE_ID in topic:
            self.handle_gate_command(data)
        else:
            self.handle_generic_light_command(node_id, device_id, data)

    # ==========================================================================
    # Smart Light / LED RGB / Relay Emulation
    # ==========================================================================
    def handle_generic_light_command(self, node_id, device_id, data):
        action = data.get("action", "").upper()
        command_id = data.get("commandId", "")
        params = data.get("parameters", {})
        if not isinstance(params, dict):
            params = {}

        if not hasattr(self, "light_states"):
            self.light_states = {}

        if device_id not in self.light_states:
            self.light_states[device_id] = {
                "power": "OFF",
                "brightness": 80,
                "r": 255,
                "g": 255,
                "b": 255,
                "color": {"r": 255, "g": 255, "b": 255}
            }

        cur = self.light_states[device_id]
        print(f"  {C_YELLOW}→ [SMART LIGHT / LED CONTROLLER]{C_RESET} Executing action: {C_BOLD}{action}{C_RESET} on device {device_id}")

        if action in ("POWER_ON", "TURN_ON"):
            cur["power"] = "ON"
        elif action in ("POWER_OFF", "TURN_OFF"):
            cur["power"] = "OFF"
        elif action == "SET_POWER":
            p = data.get("power", params.get("power", True))
            cur["power"] = "ON" if str(p).upper() in ("TRUE", "ON", "1") else "OFF"
        elif action == "SET_BRIGHTNESS":
            br = data.get("brightness", params.get("brightness", cur.get("brightness", 80)))
            try:
                cur["brightness"] = max(0, min(100, int(br)))
                if cur["brightness"] > 0:
                    cur["power"] = "ON"
            except Exception:
                pass
        elif action in ("SET_RGB", "SET_COLOR"):
            r = data.get("r", params.get("r", cur.get("r", 255)))
            g = data.get("g", params.get("g", cur.get("g", 255)))
            b = data.get("b", params.get("b", cur.get("b", 255)))
            try:
                r_int = max(0, min(255, int(r)))
                g_int = max(0, min(255, int(g)))
                b_int = max(0, min(255, int(b)))
                cur["r"] = r_int
                cur["g"] = g_int
                cur["b"] = b_int
                cur["color"] = {"r": r_int, "g": g_int, "b": b_int}
                cur["power"] = "ON"
            except Exception:
                pass
            if "brightness" in data or "brightness" in params:
                br = data.get("brightness", params.get("brightness"))
                try:
                    cur["brightness"] = max(0, min(100, int(br)))
                except Exception:
                    pass

        # Send instant ACK (<15ms)
        ack_topic = f"hesta/nodes/{node_id}/devices/{device_id}/ack"
        state_topic = f"hesta/nodes/{node_id}/devices/{device_id}/state"

        if command_id:
            ack_payload = {
                "commandId": command_id,
                "deviceId": device_id,
                "status": "SUCCESS",
                "errorCode": None,
                "state": dict(cur),
                "timestamp": int(time.time() * 1000)
            }
            self.client.publish(ack_topic, json.dumps(ack_payload), qos=1)
            print(f"  {C_GREEN}✔ [OUTBOUND ACK PUBLISHED]{C_RESET} Topic: {ack_topic} | State: {cur}")

        # Publish state update
        state_payload = dict(cur)
        state_payload["deviceId"] = device_id
        self.client.publish(state_topic, json.dumps(state_payload), qos=1)

    # ==========================================================================
    # Casper AC Emulation
    # ==========================================================================
    def handle_ac_command(self, data):
        action = data.get("action", "").upper()
        command_id = data.get("commandId", "")
        params = data.get("parameters", {})
        if not isinstance(params, dict):
            params = {}

        print(f"  {C_BLUE}→ [CASPER AC MOTOR CONTROLLER]{C_RESET} Executing action: {C_BOLD}{action}{C_RESET}")

        if action in ("SET_POWER", "POWER_ON", "POWER_OFF", "TURN_ON", "TURN_OFF"):
            if "power" in data:
                p_val = data["power"]
                self.ac_state["power"] = bool(p_val) if isinstance(p_val, bool) else (str(p_val).upper() in ("TRUE", "ON", "1"))
            elif "power" in params:
                p_val = params["power"]
                self.ac_state["power"] = bool(p_val) if isinstance(p_val, bool) else (str(p_val).upper() in ("TRUE", "ON", "1"))
            elif action in ("POWER_ON", "TURN_ON"):
                self.ac_state["power"] = True
            elif action in ("POWER_OFF", "TURN_OFF"):
                self.ac_state["power"] = False

        elif action == "SET_TEMPERATURE":
            temp = data.get("temperature") or params.get("temperature") or data.get("temp")
            if temp is not None:
                self.ac_state["temperature"] = max(16, min(30, int(temp)))

        elif action in ("TEMPERATURE_PLUS", "TEMP_UP"):
            self.ac_state["temperature"] = min(30, self.ac_state["temperature"] + 1)

        elif action in ("TEMPERATURE_MINUS", "TEMP_DOWN"):
            self.ac_state["temperature"] = max(16, self.ac_state["temperature"] - 1)

        elif action == "SET_MODE":
            mode_val = data.get("mode") or params.get("mode") or "COOL"
            self.ac_state["mode"] = str(mode_val).upper()

        elif action == "SET_FAN":
            fan_val = data.get("fan") or params.get("fan") or "AUTO"
            self.ac_state["fan"] = str(fan_val).upper()

        elif action == "SET_SWING":
            sw_val = data.get("swing") if "swing" in data else params.get("swing", False)
            self.ac_state["swing"] = bool(sw_val) if isinstance(sw_val, bool) else (str(sw_val).upper() in ("TRUE", "ON", "1"))

        elif action == "SET_TIMER":
            self.ac_state["timerEnabled"] = True
            self.ac_state["timerHour"] = int(data.get("hour") or params.get("hour") or 1)
            self.ac_state["timerHalfHour"] = bool(data.get("halfHour") or params.get("halfHour", False))

        elif action == "CANCEL_TIMER":
            self.ac_state["timerEnabled"] = False
            self.ac_state["timerHour"] = 0
            self.ac_state["timerHalfHour"] = False

        # Send instant ACK (<20ms) so Spring Boot CompletableFuture resolves with 0 timeout
        if command_id:
            ack_payload = {
                "commandId": command_id,
                "deviceId": AC_DEVICE_ID,
                "status": "SUCCESS",
                "errorCode": None,
                "state": dict(self.ac_state),
                "timestamp": int(time.time() * 1000)
            }
            self.client.publish(AC_ACK_TOPIC, json.dumps(ack_payload), qos=1)
            print(f"  {C_GREEN}✔ [OUTBOUND ACK PUBLISHED]{C_RESET} Topic: {AC_ACK_TOPIC}")

        # Publish updated telemetry matching firmware
        self.publish_ac_telemetry()

    def publish_ac_telemetry(self):
        payload = {
            "nodeId": AC_NODE_ID,
            "deviceId": AC_DEVICE_ID,
            "deviceType": "AIR_CONDITIONER",
            "power": self.ac_state["power"],
            "temperature": self.ac_state["temperature"],
            "mode": self.ac_state["mode"],
            "fan": self.ac_state["fan"],
            "swing": self.ac_state["swing"],
            "timerEnabled": self.ac_state["timerEnabled"],
            "timerHour": self.ac_state["timerHour"],
            "timerHalfHour": self.ac_state["timerHalfHour"],
            "timestamp": int(time.time() * 1000)
        }
        self.client.publish(AC_TELEMETRY_TOPIC, json.dumps(payload), qos=1, retain=True)
        # Also publish state topic
        state_topic = f"hesta/nodes/{AC_NODE_ID}/devices/{AC_DEVICE_ID}/state"
        self.client.publish(state_topic, json.dumps({"deviceId": AC_DEVICE_ID, "state": self.ac_state}), qos=1, retain=True)
        print(f"  {C_CYAN}📡 [AC TELEMETRY PUBLISHED]{C_RESET} Power={self.ac_state['power']}, Temp={self.ac_state['temperature']}°C, Mode={self.ac_state['mode']}")

    # ==========================================================================
    # Sliding Gate L298N Emulation
    # ==========================================================================
    def handle_gate_command(self, data):
        action = data.get("action", "").upper()
        command_id = data.get("commandId", "")

        print(f"  {C_BLUE}→ [L298N MOTOR DRIVER]{C_RESET} Executing action: {C_BOLD}{action}{C_RESET}")

        with self.gate_lock:
            if self.gate_timer and self.gate_timer.is_alive():
                self.gate_timer.cancel()

            if action in ("OPEN", "GATE_OPEN"):
                self.gate_state = "OPENING"
                self.publish_gate_state(limit_open=False, limit_close=False)

                # Send intermediate ACK
                if command_id:
                    self._send_gate_ack(command_id, "SUCCESS")

                # Simulate motor travel (takes 2.5 seconds to reach limit switch)
                def finish_open():
                    with self.gate_lock:
                        if self.gate_state == "OPENING":
                            self.gate_state = "OPEN"
                            print(f"\n{C_GREEN}⚡ [LIMIT SWITCH HIT] Cổng đã mở hoàn toàn (OPEN LIMIT PRESSED){C_RESET}")
                            self.publish_gate_state(limit_open=True, limit_close=False)

                self.gate_timer = threading.Timer(2.5, finish_open)
                self.gate_timer.daemon = True
                self.gate_timer.start()

            elif action in ("CLOSE", "GATE_CLOSE"):
                self.gate_state = "CLOSING"
                self.publish_gate_state(limit_open=False, limit_close=False)

                if command_id:
                    self._send_gate_ack(command_id, "SUCCESS")

                # Simulate motor travel (takes 2.5 seconds to reach limit switch)
                def finish_close():
                    with self.gate_lock:
                        if self.gate_state == "CLOSING":
                            self.gate_state = "CLOSED"
                            print(f"\n{C_GREEN}⚡ [LIMIT SWITCH HIT] Cổng đã đóng hoàn toàn (CLOSE LIMIT PRESSED){C_RESET}")
                            self.publish_gate_state(limit_open=False, limit_close=True)

                self.gate_timer = threading.Timer(2.5, finish_close)
                self.gate_timer.daemon = True
                self.gate_timer.start()

            elif action in ("STOP", "GATE_STOP"):
                self.gate_state = "STOPPED"
                print(f"  {C_RED}🛑 [MOTOR STOPPED]{C_RESET} Đã dừng động cơ.")
                self.publish_gate_state(limit_open=False, limit_close=False)
                if command_id:
                    self._send_gate_ack(command_id, "SUCCESS")

    def _send_gate_ack(self, command_id, status):
        ack = {
            "commandId": command_id,
            "deviceId": GATE_DEVICE_ID,
            "status": status,
            "state": {"state": self.gate_state},
            "timestamp": int(time.time() * 1000)
        }
        self.client.publish(GATE_ACK_TOPIC, json.dumps(ack), qos=1)
        print(f"  {C_GREEN}✔ [OUTBOUND ACK PUBLISHED]{C_RESET} Topic: {GATE_ACK_TOPIC}")

    def publish_gate_state(self, limit_open=None, limit_close=None):
        if limit_open is None:
            limit_open = (self.gate_state == "OPEN")
        if limit_close is None:
            limit_close = (self.gate_state == "CLOSED")

        payload = {
            "node_id": GATE_NODE_ID,
            "deviceId": GATE_DEVICE_ID,
            "device": "gate",
            "state": self.gate_state,
            "limit_open": limit_open,
            "limit_close": limit_close,
            "wifi_rssi": -48,
            "timestamp": int(time.time() * 1000)
        }
        self.client.publish(GATE_STATE_TOPIC, json.dumps(payload), qos=1, retain=True)
        print(f"  {C_CYAN}🚪 [GATE STATE PUBLISHED]{C_RESET} State={self.gate_state} (OpenLimit={limit_open}, CloseLimit={limit_close})")

    # ==========================================================================
    # Sensors (DHT22, LDR, PIR) Emulation
    # ==========================================================================
    def publish_sensor_metric(self, topic, device_id, metric_type, value, unit):
        payload = {
            "nodeId": SENSOR_NODE_ID,
            "deviceId": device_id,
            "metricType": metric_type,
            "value": value,
            "unit": unit,
            "timestamp": int(time.time() * 1000)
        }
        self.client.publish(topic, json.dumps(payload), qos=1)

    def publish_all_sensors(self):
        # 1. DHT22
        t = round(self.sensor_data["temperature"] + random.uniform(-0.3, 0.3), 1)
        h = round(self.sensor_data["humidity"] + random.uniform(-0.5, 0.5), 1)
        self.publish_sensor_metric(DHT22_TOPIC, DHT22_DEVICE_ID, "temperature", t, "°C")
        self.publish_sensor_metric(DHT22_TOPIC, DHT22_DEVICE_ID, "humidity", h, "%")

        # 2. LDR
        lux = round(self.sensor_data["lux"] + random.uniform(-10, 10), 1)
        self.publish_sensor_metric(LDR_TOPIC, LDR_DEVICE_ID, "LIGHT_SENSOR", 1 if lux > 300 else 0, "boolean")

        # 3. PIR
        self.publish_sensor_metric(PIR_TOPIC, PIR_DEVICE_ID, "MOTION_SENSOR", self.sensor_data["motion"], "boolean")

    def trigger_motion(self, detected=True):
        self.sensor_data["motion"] = 1 if detected else 0
        self.publish_sensor_metric(PIR_TOPIC, PIR_DEVICE_ID, "MOTION_SENSOR", 1 if detected else 0, "boolean")
        status_txt = f"{C_RED}🚨 CHUYỂN ĐỘNG PHÁT HIỆN! (PIR=1){C_RESET}" if detected else f"{C_GREEN}Bình thường (PIR=0){C_RESET}"
        print(f"\n{status_txt}")

        if detected:
            # Auto-clear after 4 seconds
            def clear_motion():
                time.sleep(4)
                self.sensor_data["motion"] = 0
                self.publish_sensor_metric(PIR_TOPIC, PIR_DEVICE_ID, "MOTION_SENSOR", 0, "boolean")
                print(f"\n{C_CYAN}[PIR AUTO-RESET] Trở về trạng thái yên tĩnh (PIR=0){C_RESET}")

            threading.Thread(target=clear_motion, daemon=True).start()

    def publish_status(self, node_id, status_str="ONLINE"):
        topic = f"hesta/nodes/{node_id}/status"
        self.client.publish(topic, f'"{status_str}"', qos=1, retain=True)

    def _sensor_loop(self):
        while self.running:
            try:
                time.sleep(10)
                if self.client.is_connected():
                    self.publish_all_sensors()
            except Exception as e:
                print(f"[Sensor loop error]: {e}")

    def stop(self):
        self.running = False
        print("\nStopping hardware simulator...")
        self.client.loop_stop()
        self.client.disconnect()
        print("Done.")


def main():
    parser = argparse.ArgumentParser(description="HESTA Smart Home - Hardware Node Simulator")
    parser.add_argument("--broker", default=os.getenv("MQTT_BROKER_HOST", "localhost"), help="MQTT broker address (default: localhost)")
    parser.add_argument("--port", type=int, default=int(os.getenv("MQTT_BROKER_PORT", 1883)), help="MQTT broker port (default: 1883)")
    parser.add_argument("--username", default=os.getenv("MQTT_USERNAME", ""), help="MQTT username (optional)")
    parser.add_argument("--password", default=os.getenv("MQTT_PASSWORD", ""), help="MQTT password (optional)")
    parser.add_argument("--no-auto-sensors", action="store_true", help="Disable automatic periodic sensor telemetry")

    args = parser.parse_args()

    sim = HardwareSimulator(
        broker=args.broker,
        port=args.port,
        username=args.username if args.username else None,
        password=args.password if args.password else None,
        auto_sensors=not args.no_auto_sensors
    )
    sim.start()

    print(f"\n{C_BOLD}--- BÀN ĐIỀU KHIỂN TƯƠNG TÁC (INTERACTIVE CLI) ---{C_RESET}")
    print(f"  Gõ phím để kích hoạt giả lập cảm biến thực tế:")
    print(f"    {C_BOLD}[m]{C_RESET} : Kích hoạt Cảm biến chuyển động PIR (Motion Detected!)")
    print(f"    {C_BOLD}[t]{C_RESET} : Tăng nhiệt độ môi trường (+1°C)")
    print(f"    {C_BOLD}[d]{C_RESET} : Giảm nhiệt độ môi trường (-1°C)")
    print(f"    {C_BOLD}[l]{C_RESET} : Bật/Tắt ánh sáng mạnh/yếu (LDR)")
    print(f"    {C_BOLD}[s]{C_RESET} : Gửi ngay 1 chu kỳ Telemetry toàn bộ cảm biến")
    print(f"    {C_BOLD}[q]{C_RESET} : Thoát simulator\n")

    try:
        while True:
            cmd = input().strip().lower()
            if cmd == "q":
                break
            elif cmd == "m":
                sim.trigger_motion(detected=True)
            elif cmd == "t":
                sim.sensor_data["temperature"] += 1.0
                print(f"Nhiệt độ môi trường: {sim.sensor_data['temperature']}°C")
                sim.publish_all_sensors()
            elif cmd == "d":
                sim.sensor_data["temperature"] -= 1.0
                print(f"Nhiệt độ môi trường: {sim.sensor_data['temperature']}°C")
                sim.publish_all_sensors()
            elif cmd == "l":
                sim.sensor_data["lux"] = 150.0 if sim.sensor_data["lux"] > 300 else 600.0
                print(f"Độ sáng môi trường: {sim.sensor_data['lux']} lux")
                sim.publish_all_sensors()
            elif cmd == "s":
                sim.publish_all_sensors()
                print("Đã gửi chu kỳ Telemetry.")
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        sim.stop()


if __name__ == "__main__":
    main()
