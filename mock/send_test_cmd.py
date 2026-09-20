import paho.mqtt.client as mqtt
import json
import time

BROKER = "localhost"
PORT = 1883
COMMAND_TOPIC = "hesta/nodes/node_1/devices/light_1/command"

client = mqtt.Client()
client.connect(BROKER, PORT, 60)

payload = {
    "version": "1.0",
    "commandId": "cmd-test-1234",
    "deviceId": "light_1",
    "action": "TURN_ON",
    "parameters": {"brightness": 100},
    "timestamp": int(time.time() * 1000)
}

print(f"Sending command to {COMMAND_TOPIC}...")
client.publish(COMMAND_TOPIC, json.dumps(payload), qos=1)
client.disconnect()
print("Sent.")