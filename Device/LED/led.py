import asyncio

import json

import threading



import paho.mqtt.client as mqtt

from ble_led import BleLedController



# =================================================

# CONFIG

# =================================================



MQTT_BROKER = "127.0.0.1"

MQTT_PORT = 1883



# Lắng nghe cả chuẩn mới trên develop và chuẩn cũ

MQTT_TOPICS = [

    ("hesta/nodes/+/devices/+/command", 1),

    ("hesta/device/+/command", 1),

    ("hesta/device/led-strip-01/command", 1),

]



BLE_ADDRESS = "BE:59:E6:00:0F:73"



# =================================================

# GLOBAL EVENT LOOP

# =================================================



loop = asyncio.new_event_loop()





def run_loop():

    asyncio.set_event_loop(loop)

    loop.run_forever()





threading.Thread(target=run_loop, daemon=True).start()



# =================================================

# BLE CONTROLLER

# =================================================



led = BleLedController(BLE_ADDRESS)





async def connect_ble_with_retry():

    while True:

        try:

            await led.connect()

            print("[BLE] Connected & Ready")

            return

        except Exception as e:

            print("[BLE] Connection failed:", e)

            print("[BLE] Retrying in 5 seconds...")

            await asyncio.sleep(5)





asyncio.run_coroutine_threadsafe(connect_ble_with_retry(), loop).result()



# =================================================

# ASYNC ACTION HANDLER + ACK & STATE

# =================================================



async def execute_command(action, params, command_id, node_code, device_id, client):

    current_state = {}

    success = True

    error_message = None



    try:

        # 1. POWER_ON / TURN_ON

        if action in ("POWER_ON", "TURN_ON"):

            await led.power_on()

            current_state = {"power": "ON"}

            print("[BLE] Executed POWER_ON")



        # 2. POWER_OFF / TURN_OFF

        elif action in ("POWER_OFF", "TURN_OFF"):

            await led.power_off()

            current_state = {"power": "OFF"}

            print("[BLE] Executed POWER_OFF")



        # 3. SET_RGB

        elif action == "SET_RGB":

            r = int(params.get("r", 255))

            g = int(params.get("g", 255))

            b = int(params.get("b", 255))

            await led.set_rgb(r, g, b)

            current_state = {"power": "ON", "r": r, "g": g, "b": b}

            print(f"[BLE] Executed SET_RGB (r={r}, g={g}, b={b})")



        # 4. SET_BRIGHTNESS

        elif action == "SET_BRIGHTNESS":

            brightness = int(params.get("brightness", 100))

            await led.set_brightness(brightness)

            current_state = {"brightness": brightness}

            if brightness > 0:

                current_state["power"] = "ON"

            print(f"[BLE] Executed SET_BRIGHTNESS ({brightness}%)")



        else:

            print(f"[MQTT] Unknown action: {action}")

            success = False

            error_message = f"Unsupported action: {action}"



    except Exception as e:

        print(f"[BLE] Error executing action {action}: {e}")

        success = False

        error_message = str(e)



    # 5. Gửi ACK về Backend để xác nhận hoàn tất lệnh (tránh Backend bị Timeout 5s)

    if command_id:

        ack_topic = f"hesta/nodes/{node_code}/devices/{device_id}/ack"

        ack_payload = {

            "commandId": command_id,

            "deviceId": device_id,

            "status": "SUCCESS" if success else "FAILED",

            "errorCode": None if success else "EXECUTION_ERROR",

            "message": error_message,

            "state": current_state if success else None,

        }

        client.publish(ack_topic, json.dumps(ack_payload), qos=1)

        # Đồng thời gửi về fallback topic cũ

        client.publish(f"hesta/device/{device_id}/ack", json.dumps(ack_payload), qos=1)

        print(f"[MQTT] Sent ACK to {ack_topic}: {ack_payload}")



    # 6. Cập nhật Device State lên Backend & Realtime UI

    if success and current_state:

        state_topic = f"hesta/nodes/{node_code}/devices/{device_id}/state"

        state_payload = {

            "deviceId": device_id,

            "state": current_state

        }

        client.publish(state_topic, json.dumps(state_payload), qos=1)

        client.publish(f"hesta/device/{device_id}/state", json.dumps(state_payload), qos=1)

        print(f"[MQTT] Sent State to {state_topic}: {state_payload}")





# =================================================

# MQTT CALLBACKS

# =================================================



def on_connect(client, userdata, flags, rc, properties=None):

    if rc == 0:

        print("[MQTT] Connected successfully")

        client.subscribe(MQTT_TOPICS)

        for topic, qos in MQTT_TOPICS:

            print(f"[MQTT] Subscribed: {topic}")

    else:

        print(f"[MQTT] Connection failed with code: {rc}")





def on_message(client, userdata, msg):

    topic = msg.topic

    raw_text = msg.payload.decode("utf-8", errors="replace")



    print("\n-------------------------------------------")

    print(f"[MQTT] Inbound Topic: {topic}")

    print(f"[MQTT] Payload: {raw_text}")



    try:

        payload = json.loads(raw_text)

    except Exception as e:

        print("[MQTT] JSON parse error:", e)

        return



    # Trích xuất node_code & device_id từ topic

    parts = topic.split("/")

    node_code = "unknown"

    device_id = "led-strip-01"



    if len(parts) >= 6 and parts[1] == "nodes" and parts[3] == "devices":

        node_code = parts[2]

        device_id = parts[4]

    elif len(parts) >= 4 and parts[1] == "device":

        device_id = parts[2]



    # Target trong payload nếu có

    if payload.get("target"):

        device_id = payload.get("target")



    command_id = payload.get("commandId")

    action = payload.get("action")



    # Đọc tham số (hỗ trợ cả nested 'parameters' lẫn root fields)

    params = payload.get("parameters", {})

    if not isinstance(params, dict):

        params = {}



    for key in ("r", "g", "b", "brightness"):

        if key in payload and key not in params:

            params[key] = payload[key]



    # Đẩy tác vụ sang event loop BLE chạy bất đồng bộ

    asyncio.run_coroutine_threadsafe(

        execute_command(action, params, command_id, node_code, device_id, client),

        loop

    )





# =================================================

# MQTT CLIENT INIT

# =================================================



client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

client.on_connect = on_connect

client.on_message = on_message



print(f"[MQTT] Connecting to {MQTT_BROKER}:{MQTT_PORT}...")

client.connect(MQTT_BROKER, MQTT_PORT, 60)



print("[GATEWAY] Running...")

client.loop_forever()