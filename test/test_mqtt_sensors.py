import paho.mqtt.client as mqtt
import json

BROKER = "localhost" # Toàn tự đổi thành IP của Mosquitto nếu chạy khác máy
PORT = 1883

print("========================================")
print("   TOOL TEST MQTT LẮNG NGHE SENSOR      ")
print("========================================")
print("Đang kết nối tới MQTT Broker...")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[+] Đã kết nối MQTT Broker {BROKER} thành công!")
        
        catalog_topic = "hesta/nodes/+/catalog"
        state_topic = "hesta/nodes/+/devices/+/state"
        
        client.subscribe([(catalog_topic, 0), (state_topic, 0)])
        print(f"[*] Đang lắng nghe mảng Catalog tại: {catalog_topic}")
        print(f"[*] Đang lắng nghe trạng thái Sensor tại: {state_topic}\n")
    else:
        print("Lỗi kết nối MQTT!")

def on_message(client, userdata, msg):
    print("--------------------------------------------------")
    print(f"💌 BẮT ĐƯỢC GÓI TIN TỪ TOPIC: {msg.topic}")
    try:
        data = json.loads(msg.payload.decode())
        print(json.dumps(data, indent=2, ensure_ascii=False))
    except:
        print(msg.payload.decode())

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(BROKER, PORT, 60)
    client.loop_forever()
except KeyboardInterrupt:
    print("\nThoát.")
