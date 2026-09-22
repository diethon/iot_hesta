import paho.mqtt.client as mqtt
import json
import time
import argparse
import sys

# Constants
BROKER = "localhost"
PORT = 1883
COMMAND_TOPIC = "hesta/nodes/+/devices/+/command"

# Global state to keep track of current device state
device_states = {}

def get_topic_parts(topic):
    # Example: hesta/nodes/node_123/devices/light_01/command
    parts = topic.split('/')
    if len(parts) >= 6:
        return parts[2], parts[4]  # nodeId, deviceId
    return "unknown", "unknown"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[{userdata['mode']}] Connected to MQTT Broker at {BROKER}:{PORT}")
        client.subscribe(COMMAND_TOPIC)
        print(f"Subscribed to {COMMAND_TOPIC}")
    else:
        print(f"Failed to connect, return code {rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    node_id, device_id = get_topic_parts(topic)
    
    try:
        payload = json.loads(msg.payload.decode('utf-8'))
    except json.JSONDecodeError:
        print(f"[-] Invalid JSON on {topic}")
        return

    command_id = payload.get("commandId", "unknown")
    action = payload.get("action", "")
    parameters = payload.get("parameters", {})
    
    print(f"\n[+] Received Command: {action} for Device: {device_id} (CmdID: {command_id})")
    
    # -----------------------------
    # 1. Check Simulation Mode
    # -----------------------------
    mode = userdata['mode']
    if mode == "OFFLINE":
        print("  -> OFFLINE mode: Ignoring message.")
        return
        
    if mode == "TIMEOUT":
        print("  -> TIMEOUT mode: Sleeping for 15s to simulate timeout...")
        time.sleep(15)
        # Often a timeout means we don't even ACK, or we ACK very late
        print("  -> TIMEOUT mode: Dropping packet (no ACK).")
        return

    # -----------------------------
    # 2. Build ACK Payload
    # -----------------------------
    ack_topic = f"hesta/nodes/{node_id}/devices/{device_id}/ack"
    
    ack_status = "SUCCESS"
    error_code = None
    
    if mode == "ERROR":
        ack_status = "FAILED"
        error_code = "SIMULATED_ERROR"
        print("  -> ERROR mode: Sending FAILED ACK.")
        
    ack_payload = {
        "version": "1.0",
        "commandId": command_id,
        "deviceId": device_id,
        "status": ack_status,
        "errorCode": error_code,
        "timestamp": int(time.time() * 1000)
    }
    
    client.publish(ack_topic, json.dumps(ack_payload), qos=1)
    print(f"  -> Published ACK to {ack_topic}")
    
    # If error, don't update state
    if ack_status == "FAILED":
        return
        
    # -----------------------------
    # 3. Process Action & Build State
    # -----------------------------
    # Retrieve current state or init default
    if device_id not in device_states:
        device_states[device_id] = {"power": "OFF", "brightness": 0}
        
    current_state = device_states[device_id]
    
    if action == "TURN_ON":
        current_state["power"] = "ON"
    elif action == "TURN_OFF":
        current_state["power"] = "OFF"
    elif action == "SET_BRIGHTNESS":
        current_state["power"] = "ON"
        if "brightness" in parameters:
            current_state["brightness"] = parameters["brightness"]
            
    device_states[device_id] = current_state
    
    state_topic = f"hesta/nodes/{node_id}/devices/{device_id}/state"
    state_payload = {
        "version": "1.0",
        "deviceId": device_id,
        "state": current_state,
        "timestamp": int(time.time() * 1000)
    }
    
    client.publish(state_topic, json.dumps(state_payload), qos=1, retain=True)
    print(f"  -> Published STATE to {state_topic}: {json.dumps(current_state)}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ESP32 Mock MQTT Simulator")
    parser.add_argument("--mode", choices=["NORMAL", "TIMEOUT", "ERROR", "OFFLINE"], default="NORMAL",
                        help="Simulation mode for testing failure scenarios")
    parser.add_argument("--node-id", default="node_01", help="Node ID for Heartbeat and LWT")
    args = parser.parse_args()

    client = mqtt.Client(userdata={"mode": args.mode})
    client.on_connect = on_connect
    client.on_message = on_message
    
    # Last Will and Testament (LWT)
    status_topic = f"hesta/nodes/{args.node_id}/status"
    client.will_set(status_topic, json.dumps({"status": "OFFLINE"}), qos=1, retain=True)

    try:
        client.connect(BROKER, PORT, 60)
        client.loop_start()
        
        while True:
            # Publish Heartbeat ONLINE every 30 seconds
            client.publish(status_topic, json.dumps({"status": "ONLINE"}), qos=1, retain=True)
            time.sleep(30)
            
    except ConnectionRefusedError:
        print(f"Error: Connection refused. Is Mosquitto running at {BROKER}:{PORT}?")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nExiting...")
        client.disconnect()
        sys.exit(0)