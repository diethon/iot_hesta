# Hướng Dẫn Sử Dụng Trình Giả Lập Phần Cứng IoT (HESTA Simulator)

Trình giả lập `simulator.py` mô phỏng chính xác hành vi của 3 bo mạch vi điều khiển ESP32 thật trong dự án HESTA, kết nối trực tiếp vào MQTT Broker để hoàn thành chu trình khép kín:
**Web (ReactJS) / Mobile App (Flutter) ⇄ Backend Spring Boot ⇄ MQTT Broker ⇄ Thiết bị phần cứng / Giả lập phần cứng**.

---

## 1. Danh Sách 3 Thiết Bị Thực Tế Được Giả Lập

| Thiết Bị | Node Code | Device ID / Local ID | Command Topic | Phản hồi Telemetry & ACK |
| :--- | :--- | :--- | :--- | :--- |
| **❄️ Điều hòa Casper IR** | `32442d7f-dfcf-4b2f-a380-adba32876cec` | `05d8a4fd-d7f1-4c62-a70e-a4213552b054` | `.../command` | Trả lời ACK `<20ms` vào `.../ack` & phát `.../telemetry` |
| **🚪 Cổng trượt tự động L298N** | `8d1cdd82-b339-469e-be13-7e91070f7ae5` | `2d566d0a-f7d6-4fac-a0bf-18912da4ab28` | `.../command` | Mô phỏng hành trình motor 2.5s chạm công tắc hành trình, phát `.../state` & `.../ack` |
| **🌡️ Bộ cảm biến DHT22, LDR, PIR** | `109441d8-b6bf-4a18-bdf8-9279b7a1548d` | DHT22: `0bb7dc6e...`<br>LDR: `57c13343...`<br>PIR: `5e854e0e...` | N/A | Tự động phát chu kỳ đo nhiệt độ, độ ẩm, độ sáng và cảm biến người |

---

## 2. Cách Chạy Trình Giả Lập

### Bước 1: Đảm bảo đã cài đặt Python và thư viện
```bash
pip install paho-mqtt
```

### Bước 2: Khởi động Simulator
Nếu MQTT Broker chạy trên máy local (hoặc Docker):
```bash
python simulator.py --broker localhost --port 1883
```

Nếu MQTT Broker chạy trên IP mạng LAN (ví dụ `192.168.88.121` hoặc `192.168.88.104`):
```bash
python simulator.py --broker 192.168.88.121 --port 1883
```

---

## 3. Các Phím Tắt Điều Khiển Trực Tiếp Trên Terminal
Khi simulator đang chạy, bạn có thể gõ các phím sau vào terminal để kiểm tra phản ứng của hệ thống:
- `m` : Kích hoạt Cảm biến chuyển động PIR (**Motion Detected!**) $\rightarrow$ Tự động reset về 0 sau 4 giây.
- `t` : Tăng nhiệt độ môi trường lên +1°C (gửi ngay telemetry cập nhật).
- `d` : Giảm nhiệt độ môi trường xuống -1°C.
- `l` : Đảo trạng thái cảm biến ánh sáng LDR (Sáng / Tối).
- `s` : Gửi ngay 1 chu kỳ Telemetry toàn bộ cảm biến.
- `q` : Thoát trình giả lập an toàn.

---

## 4. Kiểm Thử Chu Trình Hoàn Chỉnh (End-to-End Test Loop)
1. Bật Simulator: `python simulator.py --broker localhost --port 1883`
2. Mở Web ReactJS: vào trang **Thiết bị trong nhà** (`/home/:homeId/devices`).
3. Bấm **`[+ Thêm Thiết Bị]`** $\rightarrow$ chọn **"Kích hoạt nhanh Thiết bị Thật"** $\rightarrow$ bấm **"Kích hoạt toàn bộ thiết bị thật"**.
4. Các card **Casper AC**, **Cổng trượt**, **Cảm biến** sẽ xuất hiện ngay lập tức trên màn hình.
5. Khi bạn bấm nút Bật/Tắt điều hòa, chỉnh nhiệt độ, hoặc bấm Mở/Đóng cổng:
   - Trên terminal của `simulator.py`, bạn sẽ thấy log màu tím `[INBOUND COMMAND RECEIVED]`.
   - Simulator phản hồi ngay ACK `<20ms` và gửi State mới.
   - Trên giao diện ReactJS, nút và vòng quay nhiệt độ sẽ cập nhật xác nhận ngay lập tức, **không bao giờ bị Timeout**!
