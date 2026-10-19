# Hesta Smart Home - IoT Firmware & Hardware Control

> **Đồ án Tốt nghiệp Kỹ sư Kỹ thuật Phần mềm (SEP490) - Đại học FPT**  
> **Repository:** [iot_hesta](https://github.com/diethon/iot_hesta.git)

---

## 📖 Giới thiệu (Overview)

**Hesta IoT** là phân hệ phần cứng và firmware trong hệ sinh thái nhà thông minh **HESTA**. Phân hệ này chịu trách nhiệm thu thập dữ liệu môi trường từ các cảm biến, đồng thời điều khiển các thiết bị chấp hành (Actuators) như cổng tự động, máy điều hòa, quạt, đèn LED và động cơ thông qua mạng nội bộ.

Hệ thống được thiết kế theo hướng **Zero-Configuration** và **Plug-and-Play**:
- **BLE Provisioning:** Cấu hình thông tin Wi-Fi nhanh chóng qua ứng dụng di động thông qua Bluetooth Low Energy (BLE), lưu trữ bảo mật trong bộ nhớ NVS (Non-Volatile Storage).
- **mDNS Auto-Discovery:** Tự động phát hiện địa chỉ IP và Port của MQTT Broker trong mạng LAN bằng giao thức DNS-SD/mDNS (`_syna-mqtt._tcp`), loại bỏ việc phải cấu hình IP tĩnh vào mã nguồn.
- **MQTT Chuẩn hóa:** Giao tiếp hai chiều theo cấu trúc Topic phân cấp rõ ràng theo `Node ID` và `Device ID`.

---

## 🏗️ Kiến trúc Hệ thống (System Architecture)

```mermaid
graph TD
    App[📱 Hesta Mobile App] -- "1. BLE Provisioning (WiFi SSID/Pass)" --> ESP[📡 ESP32 / ESP32-CAM Nodes]
    Avahi[🐧 Linux Server / Pi (Avahi Daemon)] -- "2. mDNS Broadcast (_syna-mqtt._tcp)" --> ESP
    ESP -- "3. Auto Connect MQTT" --> Broker[📨 Mosquitto MQTT Broker]
    Broker <--> Backend[🖥️ Hesta Backend Server]
    
    subgraph "Các Phân Hệ Thiết Bị (Hardware Modules)"
        ESP --> Gate[🚪 Cổng trượt tự động (L298N + Limit Switches)]
        ESP --> AC[❄️ Điều hòa không khí (IR Blaster: Daikin, Casper)]
        ESP --> Fan[💨 Quạt & Động cơ (Relay Modules)]
        ESP --> Sensors[🌡️ Cảm biến (DHT22, LDR, PIR)]
        ESP --> LED[💡 Đèn thông minh (BLE LED Strip Controller)]
        ESP --> Servo[⚙️ Động cơ Servo (MG996R)]
    end
```

---

## 📂 Cấu trúc Thư mục (Project Structure)

```text
.
├── Avahi/
│   └── Avahi_Linux/
│       └── Test_avahi.ino             # Kiểm thử mDNS & BLE Provisioning trên ESP32
│
├── Device/                            # Firmware điều khiển thiết bị chấp hành (Actuators)
│   ├── AC/                            # Điều khiển điều hòa không khí qua tín hiệu hồng ngoại (IR)
│   │   ├── 1_ir_sniffer/              # Sniffer tín hiệu IR từ remote gốc (VS1838B)
│   │   ├── 2_daikin_control/          # Điều khiển điều hòa Daikin qua IR
│   │   ├── 3_daikin64_control/        # Giao thức Daikin 64-bit
│   │   ├── CasperACFrameBuilder/      # Bộ dựng khung truyền tín hiệu cho điều hòa Casper
│   │   ├── MQTT_AC/                   # Node điều hòa hoàn chỉnh kết nối MQTT (State & Telemetry)
│   │   ├── MQTT_AC_DAIKIN/            # Điều khiển Daikin tích hợp MQTT
│   │   ├── Receiver_AC/               # Module thu tín hiệu IR
│   │   ├── TestAC_IRLED_TO_VS1838/    # Kiểm thử vòng lặp truyền - nhận IR
│   │   └── Danh sách bytecodeAC.docx  # Tài liệu phân tích Bytecode điều hòa
│   │
│   ├── Control_Gate_Motor_L298N/      # Điều khiển cổng tự động
│   │   ├── Control_Gate_Motor_L298N.ino # Điều khiển động cơ cổng qua L298N & công tắc hành trình
│   │   ├── BLE_Control_Gate/          # Thử nghiệm điều khiển cổng qua BLE
│   │   └── Control_Gate_Motor_BLE_mDNS/ # Node cổng trượt: BLE Provisioning + mDNS + MQTT
│   │
│   ├── Fan_Motor/                     # Điều khiển Quạt & Động cơ
│   │   └── Fan_Motor.ino              # Relay 2 kênh, hỗ trợ BLE Provisioning, NVS, mDNS & MQTT
│   │
│   ├── LED/                           # Điều khiển LED thông minh
│   │   └── led.py                     # Cầu nối Python: MQTT Client sang BLE LED Strip
│   │
│   └── Servo/                         # Động cơ Servo
│       └── Test_Servo.ino             # Điều khiển Servo MG996R theo góc quay PWM
│
└── Sensor/                            # Phân hệ cảm biến và thu thập dữ liệu
    ├── BLE_Sensor_MQTT/               # Node cảm biến trung tâm: DHT22, LDR, PIR + BLE + mDNS
    ├── mqtt_connect_db/               # Gửi dữ liệu cảm biến lên Broker
    └── sensor_data_collection/        # Thu thập dữ liệu cảm biến thô
```

---

## ⚙️ Các Tính năng Kỹ thuật Chi tiết

### 1. BLE Provisioning (Cài đặt Wi-Fi không chạm)
- **Service UUID:** `12345678-1234-1234-1234-123456789000`
- **Characteristic UUID:** `12345678-1234-1234-1234-123456789001` (Read/Write/Notify)
- **Cơ chế:**
  - Ứng dụng di động quét và kết nối với thiết bị qua BLE.
  - Gửi gói tin JSON:
    ```json
    {
      "action": "set_wifi",
      "ssid": "Your_WiFi_SSID",
      "password": "Your_WiFi_Password"
    }
    ```
  - Thiết bị lưu thông tin vào phân vùng NVS (Preferences) để tự động kết nối lại sau khi khởi động.
  - Trả về thông báo trạng thái kết nối (`WIFI_CONNECTING`, `WIFI_CONNECTED`, `WIFI_FAILED`).

### 2. mDNS Auto-Discovery (Khám phá dịch vụ Broker)
- Thiết bị tìm kiếm dịch vụ mDNS `_syna-mqtt._tcp` trên mạng cục bộ.
- Tự động lấy địa chỉ IP và cổng dịch vụ (Port 1883) mà không cần cấu hình thủ công.
- Có cơ chế IP Fallback dự phòng nếu mDNS không phản hồi.

### 3. Chuẩn Topic MQTT
Các Topic tuân thủ cấu trúc phân cấp:

| Loại Topic | Mẫu Topic | Chức năng |
| :--- | :--- | :--- |
| **Command** | `hesta/nodes/{nodeId}/devices/{deviceId}/command` | Nhận lệnh điều khiển từ Backend |
| **Telemetry** | `hesta/nodes/{nodeId}/devices/{deviceId}/telemetry` | Gửi dữ liệu định kỳ (Nhiệt độ, Độ ẩm, Cường độ sáng, ...) |
| **Status** | `hesta/nodes/{nodeId}/devices/{deviceId}/status` | Báo cáo trạng thái kết nối (`ONLINE`, `OFFLINE`) và trạng thái thiết bị |

---

## 🔌 Phần cứng & Linh kiện sử dụng

- **Vi điều khiển:**
  - ESP32 NodeMCU (30 pins / 38 pins)
  - ESP32-CAM AI-Thinker
  - ESP32-S3
- **Cảm biến:**
  - Cảm biến nhiệt độ & độ ẩm: `DHT22` (AM2302)
  - Cảm biến cường độ ánh sáng: `LDR` (Photoresistor)
  - Cảm biến chuyển động hồng ngoại: `PIR` (HC-SR501)
  - Mắt thu tín hiệu hồng ngoại: `VS1838B` (38kHz)
- **Cơ cấu chấp hành:**
  - Mạch công suất cầu H: `L298N Dual H-Bridge Driver`
  - Động cơ giảm tốc: `TT DC Gear Motor`
  - Công tắc hành trình: `Endstop / Limit Switch` (NO/NC)
  - Module Relay kích mức thấp (Low-level trigger)
  - Mắt phát hồng ngoại: `IR LED 940nm`
  - Động cơ Servo: `TowerPro MG996R`
  - Dải LED thông minh Bluetooth

---

## 💻 Yêu cầu Phần mềm & Thư viện (Libraries)

Để biên dịch và nạp code cho các vi điều khiển ESP32, cần cài đặt **Arduino IDE** hoặc **PlatformIO (VS Code)** với ESP32 Board Core (phiên bản `>= 2.0.x`) cùng các thư viện sau:

- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT Client)
- [ArduinoJson](https://arduinojson.org/) (phiên bản 6.x hoặc 7.x)
- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) (Điều khiển và giải mã tín hiệu IR)
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) & Adafruit Unified Sensor
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) hoặc thư viện BLE mặc định của ESP32

---

## 🚀 Hướng dẫn Triển khai & Chạy thử

### 1. Cấu hình mDNS Service trên Linux (Tùy chọn)
Nếu bạn có một máy chủ Linux hoặc Raspberry Pi đang chạy Mosquitto Broker, hãy tạo file cấu hình Avahi để các node ESP32 tự tìm thấy Broker:
```bash
sudo nano /etc/avahi/services/syna-mqtt.service
```
Nội dung:
```xml
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name replace-wildcards="yes">Syna MQTT Broker</name>
  <service>
    <type>_syna-mqtt._tcp</type>
    <port>1883</port>
  </service>
</service-group>
```
Khởi động lại dịch vụ:
```bash
sudo systemctl restart avahi-daemon
```

### 2. Biên dịch & Nạp Code vào ESP32
1. Mở file `.ino` tương ứng với thiết bị cần nạp trong Arduino IDE (ví dụ: `Device/Fan_Motor/Fan_Motor.ino`).
2. Chọn Board: **ESP32 Dev Module** (hoặc **AI Thinker ESP32-CAM**).
3. Chọn cổng COM tương ứng và tiến hành **Upload**.
4. Mở Serial Monitor với baud rate `115200` để theo dõi tiến trình khởi động.

### 3. Cấu hình Wi-Fi qua BLE
1. Sử dụng ứng dụng di động Hesta hoặc ứng dụng nRF Connect / LightBlue trên điện thoại.
2. Tìm thiết bị BLE có tên bắt đầu bằng `SYNA-` (ví dụ: `SYNA-FAN-MOTOR`, `SYNA-GATE-CAM`).
3. Kết nối và ghi payload JSON chứa `ssid` và `password` vào Characteristic `...9001`.
4. Thiết bị sẽ tự kết nối Wi-Fi, tìm kiếm MQTT Broker và bắt đầu gửi nhận dữ liệu.

---


