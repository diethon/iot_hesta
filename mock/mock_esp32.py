import json
import time

def generate_ack_payload(command_id, device_id):
    return json.dumps({
        "version": "1.0",
        "commandId": command_id,
        "deviceId": device_id,
        "status": "SUCCESS",
        "errorCode": None,
        "timestamp": int(time.time() * 1000)
    })

def generate_state_payload(device_id, state_dict):
    return json.dumps({
        "version": "1.0",
        "deviceId": device_id,
        "state": state_dict,
        "timestamp": int(time.time() * 1000)
    })

# Mock MQTT Client logic (Simulation)
def on_message(topic, payload):
    print(f"[ESP32] Nhận lệnh từ: {topic}")
    data = json.loads(payload)
    
    # 1. Trả về ACK
    ack_topic = topic.replace("/command", "/ack")
    ack_payload = generate_ack_payload(data["commandId"], data["deviceId"])
    print(f"[ESP32] Trả lời ACK ({ack_topic}): {ack_payload}")
    
    # 2. Xử lý lệnh vật lý và bắn State mới
    state_topic = topic.replace("/command", "/state")
    new_state = {}
    if data["action"] == "TURN_ON":
        new_state = {"power": "ON"}
    elif data["action"] == "TURN_OFF":
        new_state = {"power": "OFF"}
    
    # Nếu có parameters như brightness
    if "parameters" in data and "brightness" in data["parameters"]:
        new_state["brightness"] = data["parameters"]["brightness"]
        
    state_payload = generate_state_payload(data["deviceId"], new_state)
    print(f"[ESP32] Cập nhật State ({state_topic}): {state_payload}")

# --- Test chạy thử ---
if __name__ == "__main__":
    print("--- CHẠY MOCK ESP32 ---")
    mock_command_topic = "hesta/nodes/node_123/devices/light_01/command"
    mock_command_payload = json.dumps({
        "version": "1.0",
        "commandId": "cmd-8888-9999",
        "deviceId": "light_01",
        "action": "TURN_ON",
        "parameters": {"brightness": 75},
        "timestamp": int(time.time() * 1000)
    })
    
    # Gửi thử
    on_message(mock_command_topic, mock_command_payload)