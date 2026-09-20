# Hesta MQTT Topic & Payload Contract

## 1. Topic Matrix
Chuẩn chung (Base Topic): hesta/nodes/{nodeId}/devices/{deviceId}

| Suffix Topic | Chiều gửi | QoS | Retain | Mục đích (Description) |
| :--- | :--- | :--- | :--- | :--- |
| /command | Backend -> ESP32 | 1 | False | Backend ra lệnh điều khiển (Bật/Tắt, đổi màu...) |
| /ack | ESP32 -> Backend | 1 | False | ESP32 xác nhận đã nhận lệnh (Gắn kèm commandId để Backend đối chiếu) |
| /state | ESP32 -> Backend | 1 | True | ESP32 báo cáo trạng thái hiện tại. Retain=True để App mở lên thấy ngay. |
| /sensor | ESP32 -> Backend | 0 | False | Bắn dữ liệu cảm biến (Nhiệt độ, độ ẩm...) liên tục. QoS 0 cho nhẹ mạng. |
| /status | ESP32 -> Broker | 1 | True | Dùng cho LWT báo thiết bị Online/Offline. |

## 2. JSON Payload Mẫu (Sample JSON)

### 2.1. Lệnh từ Backend (Command)
Topic: hesta/nodes/node_123/devices/light_01/command
{
  "version": "1.0",
  "commandId": "cmd-1234",
  "deviceId": "light_01",
  "action": "TURN_ON",
  "parameters": {
    "brightness": 80
  },
  "timestamp": 1694500000000
}

### 2.2. ESP32 Trả lời (ACK)
Topic: hesta/nodes/node_123/devices/light_01/ack
{
  "version": "1.0",
  "commandId": "cmd-1234",
  "deviceId": "light_01",
  "status": "SUCCESS", 
  "errorCode": null,
  "timestamp": 1694500000150
}

### 2.3. Cập nhật trạng thái (State)
Topic: hesta/nodes/node_123/devices/light_01/state
{
  "version": "1.0",
  "deviceId": "light_01",
  "state": {
    "power": "ON",
    "brightness": 80
  },
  "timestamp": 1694500000200
}