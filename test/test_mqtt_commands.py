import paho.mqtt.client as mqtt
import json
import time
import uuid

BROKER = "localhost" # Toàn tự đổi thành IP của Mosquitto nếu chạy khác máy
PORT = 1883

print("========================================")
print("   TOOL TEST MQTT GỬI LỆNH XUỐNG ESP32  ")
print("========================================")
print("Ví dụ mã Node Code của mạch ESP32: ESP_3C71BF420011")
NODE_CODE = input("Nhập mã NODE_CODE của bạn: ").strip()
if not NODE_CODE:
    NODE_CODE = "ESP_123456"

print("\n--- CHỌN LỆNH TEST ---")
print("1. Bật Đèn (pin_4 - TURN_ON)")
print("2. Tắt Đèn (pin_4 - TURN_OFF)")
print("3. Đổi màu LED RGB (pin_15 - SET_COLOR)")
print("4. Phát Hồng Ngoại (pin_18 - SEND_IR_CODE)")
print("5. Nhận diện giọng nói AI (Nếu có)")

choice = input("Chọn số (1-4): ").strip()

topic = ""
payload = {
    "commandId": str(uuid.uuid4()),
    "action": "",
    "target": "",
    "parameters": {},
    "timestamp": int(time.time() * 1000)
}

if choice == "1":
    target_pin = input("Nhập chân cắm Relay (Mặc định: 4): ").strip() or "4"
    payload["target"] = f"pin_{target_pin}"
    payload["action"] = "TURN_ON"
    
elif choice == "2":
    target_pin = input("Nhập chân cắm Relay (Mặc định: 4): ").strip() or "4"
    payload["target"] = f"pin_{target_pin}"
    payload["action"] = "TURN_OFF"

elif choice == "3":
    target_pin = input("Nhập chân cắm LED RGB (Mặc định: 15): ").strip() or "15"
    payload["target"] = f"pin_{target_pin}"
    payload["action"] = "SET_COLOR"
    payload["parameters"] = {"r": 255, "g": 0, "b": 0}
    print("Màu test: Đỏ (255, 0, 0)")

elif choice == "4":
    target_pin = input("Nhập chân cắm IR (Mặc định: 18): ").strip() or "18"
    payload["target"] = f"pin_{target_pin}"
    payload["action"] = "SEND_IR_CODE"
    payload["parameters"] = {"protocol": "NEC", "code": "0x00FF00FF"}

else:
    print("Lựa chọn không hợp lệ!")
    exit()

topic = f"hesta/nodes/{NODE_CODE}/devices/{payload['target']}/command"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"\n[+] Đã kết nối MQTT Broker {BROKER}")
        print(f"[>] Đang gửi lệnh tới Topic: {topic}")
        print(f"[>] Payload JSON: {json.dumps(payload, indent=2)}")
        client.publish(topic, json.dumps(payload), qos=1)
        
        # Subscribe to ACK and STATE to verify
        ack_topic = f"hesta/nodes/{NODE_CODE}/devices/+/ack"
        state_topic = f"hesta/nodes/{NODE_CODE}/devices/+/state"
        print(f"[*] Đang chờ mạch ESP32 phản hồi ACK tại {ack_topic}...")
        client.subscribe([(ack_topic, 0), (state_topic, 0)])
    else:
        print("Lỗi kết nối MQTT!")

def on_message(client, userdata, msg):
    print(f"\n[<] NHẬN ĐƯỢC PHẢN HỒI TỪ ESP32:")
    print(f"    Topic: {msg.topic}")
    print(f"    Data : {msg.payload.decode()}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(BROKER, PORT, 60)
    client.loop_forever()
except KeyboardInterrupt:
    print("\nThoát.")
