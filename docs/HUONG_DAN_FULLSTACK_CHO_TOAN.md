# HƯỚNG DẪN CODE FULL-STACK (IoT - BACKEND - FRONTEND) DÀNH CHO TOÀN

*(Theo luật của team: Ai nhận tính năng nào phải code xuyên suốt từ dưới mạch lên tới Web/App nếu có)*

## Yêu cầu chuẩn bị
- Đảm bảo mạch đã kết nối WiFi và MQTT Broker thành công.
- Cài đặt thư viện **ArduinoJson** (phiên bản 6.x) để phân tích (parse) và đóng gói dữ liệu JSON.

> ⚠️ **LƯU Ý DÀNH RIÊNG CHO ARDUINO IDE:**
> Vì Toàncode bằng phần mềm Arduino IDE (không phải PlatformIO), Toànbắt buộc phải tuân thủ cấu trúc thư mục của nó:
> 1. File code chính bắt buộc phải đuôi `.ino` (Ví dụ: `esp32_firmware.ino`).
> 2. File `.ino` này phải nằm trong một thư mục có tên y hệt (Ví dụ thư mục `esp32_firmware/`).
> 3. Tạm thời nên gom chung các hàm MQTT, WiFi, Relay vào cùng 1 file `.ino` để test luồng cho dễ, hoặc vứt tất cả các file `.cpp`, `.h` ngang hàng (chung 1 cấp thư mục) với file `.ino`. KHÔNG dùng các thư mục con phức tạp như `src/core/` vì Arduino IDE sẽ không tự compile được!

---

## BẢNG TỪ VỰNG GIAO TIẾP VÀ NĂNG LỰC

Đây là các từ khóa chuẩn mà ESP32 phải dùng để gửi lên hoặc hứng lệnh từ App.

| Loại thiết bị (type) | Năng lực (capabilities) | Các Lệnh sẽ nhận (action) | Giải thích |
|---|---|---|---|
| `LIGHT` | `{"POWER": ["TURN_ON", "TURN_OFF", "TOGGLE"]}` | `TURN_ON`, `TURN_OFF`, `TOGGLE` | Đèn Relay |
| `LED_RGB` | `{"POWER": ["TURN_ON", "TURN_OFF", "TOGGLE"], "COLOR": ["SET_COLOR"], "BRIGHTNESS": ["SET_BRIGHTNESS"]}` | `TURN_ON`, `TURN_OFF`, `TOGGLE`, `SET_COLOR`, `SET_BRIGHTNESS` | Đèn dây đổi màu RGB |
| `SMART_PLUG` | `{"POWER": ["TURN_ON", "TURN_OFF", "TOGGLE"]}` | `TURN_ON`, `TURN_OFF`, `TOGGLE` | Ổ cắm thông minh |
| `TEMP_HUMID_SENSOR` | `{"TEMPERATURE_READ": [], "HUMIDITY_READ": []}` | (Chỉ báo cáo State lên) | Cảm biến Nhiệt Ẩm DHT22 |
| `MOTION_SENSOR` | `{"MOTION_DETECT": []}` | (Chỉ báo cáo State lên) | Cảm biến chuyển động PIR |
| `SMOKE_SENSOR` | `{"SMOKE_DETECT": []}` | (Chỉ báo cáo State lên) | Cảm biến Khói MQ-2 |
| `IR_REMOTE` | `{"IR_TRANSMIT": ["SEND_IR_CODE"]}` | `SEND_IR_CODE` | Cục phát hồng ngoại IR |
| `CAMERA_AI` | `{"AI_FALL_DETECT": ["SET_AI_MODE"], "AI_INTRUSION_DETECT": ["SET_AI_MODE"]}` | `SET_AI_MODE` | Camera giám sát Edge AI |

> 🚨 **CẢNH BÁO CỰC KỲ QUAN TRỌNG VỀ TÊN LOẠI CẢM BIẾN (TYPE):**
> Trong bảng từ vựng ở trên, Toàn **BẮT BUỘC** phải gõ chính xác các chữ `TEMP_HUMID_SENSOR`, `MOTION_SENSOR`, `SMOKE_SENSOR` vào trường `type` của JSON Catalog.
> **Lý do:** Ở Backend, Phong đã viết thuật toán chống Spam DB (Throttling) dựa trên việc quét chữ `SENSOR` ở đuôi. Nếu Toàn tự bịa ra tên khác (Ví dụ: `DHT22_MODULE` hay `PIR`), Backend sẽ không nhận diện được đó là cảm biến, dẫn đến việc ghi log tràn lan làm sập Database máy chủ!

---

## BẢN CHẤT CỦA NĂNG LỰC (CAPABILITY), LỆNH (ACTION) VÀ THAM SỐ (PARAMETERS) - BẮT BUỘC ĐỌC

Để Web, App và ESP32 hiểu nhau tuyệt đối, chúng ta chia giao tiếp thành 3 khái niệm:
1. **Năng lực (Capability):** Mạch ESP32 khai báo mình *có thể làm gì*. Web dựa vào đây để **VẼ GIAO DIỆN (UI)**.
   - Thấy chữ `POWER` -> Vẽ nút bấm Bật/Tắt.
   - Thấy chữ `COLOR` -> Vẽ Bảng pha màu.
2. **Lệnh (Action):** Khi User bấm trên Web, Web gửi "Động từ" xuống mạch ESP32.
3. **Tham số (Parameters):** Gửi kèm theo "Động từ" (Ví dụ: đặt màu thì phải nói rõ màu gì `r, g, b`).

### 📌 VÍ DỤ CHI TIẾT TỪNG THIẾT BỊ ĐỂ TOÀN XỬ LÝ (PARSE JSON):

#### 1. Đèn Relay (LIGHT) & Ổ cắm thông minh (SMART_PLUG)
- ESP32 khai báo Capability: `{"POWER": ["TURN_ON", "TURN_OFF", "TOGGLE"]}`
- Khi User bấm nút bật: ESP32 nhận lệnh `{"action": "TURN_ON", "parameters": {}}`
- Khi User bấm nút tắt: ESP32 nhận lệnh `{"action": "TURN_OFF", "parameters": {}}`

#### 2. Đèn dải đổi màu (LED_RGB)
- ESP32 khai báo Capability: `{"POWER": ["TURN_ON", "TURN_OFF", "TOGGLE"], "COLOR": ["SET_COLOR"], "BRIGHTNESS": ["SET_BRIGHTNESS"]}`
- Bật/Tắt: ESP32 nhận lệnh `{"action": "TURN_ON", "parameters": {}}`
- Đổi màu đỏ: ESP32 nhận lệnh `{"action": "SET_COLOR", "parameters": {"r": 255, "g": 0, "b": 0}}`
- Giảm sáng 50%: ESP32 nhận lệnh `{"action": "SET_BRIGHTNESS", "parameters": {"level": 50}}` *(Mạch tự giảm cường độ PWM của 3 bóng LED mà không đổi màu)*.

#### 3. Cục phát tia Hồng ngoại (IR_REMOTE)
- ESP32 khai báo Capability: `{"IR_TRANSMIT": ["SEND_IR_CODE"]}`
- Khi User bấm "Bật Máy Lạnh" trên Web, ESP32 nhận lệnh: 
  `{"action": "SEND_IR_CODE", "parameters": {"protocol": "NEC", "code": "0x00FF00FF"}}`
  *(Mạch ESP32 đọc chữ NEC và cái mã HEX để cấp xung điện cho chân Transistor 2N2222 nhấp nháy IR)*.

#### 4. Cảm biến Nhiệt/Ẩm (TEMP_HUMID_SENSOR)
- Khai báo Capability: `{"TEMPERATURE_READ": [], "HUMIDITY_READ": []}`
- **Lưu ý:** Loại này Web KHÔNG bao giờ gửi Lệnh (Action) xuống. 
- ESP32 chỉ tự động gửi STATE lên Web mỗi 10 giây:
  `{"state": {"temperature": 27.5, "humidity": 80.2}}`

#### 5. Cảm biến Chuyển động (MOTION_SENSOR) & Khói (SMOKE_SENSOR)
- Khai báo Capability: `{"MOTION_DETECT": []}` hoặc `{"SMOKE_DETECT": []}`
- Tương tự như DHT22, mạch chỉ bắn STATE lên khi có người đi qua hoặc có khói:
  - Cảm biến PIR: `{"state": {"motion": true}}`
  - Cảm biến Khói: `{"state": {"smoke": true}}`

#### 6. Camera Giám Sát Orange Pi (CAMERA_AI)
- Khai báo Capability: `["AI_FALL_DETECT", "AI_INTRUSION_DETECT"]`
- Khi User bấm nút "Bật chế độ chống trộm":
  Orange Pi nhận lệnh: `{"action": "SET_AI_MODE", "parameters": {"mode": "INTRUSION_ONLY", "active": true}}`
- Khi Camera phát hiện té ngã, nó tự bắn STATE lên: `{"state": {"fallDetected": true, "confidence": 0.95}}`

---
---

## PHẦN I: CODE C++ CHO MẠCH ESP32 (KIẾN TRÚC TRẠM ĐIỀU KHIỂN CHÂN GPIO)

Thay vì cấu hình cứng danh sách từng cái đèn riêng biệt trên mạch, mạch ESP32 của Toàn giờ đây sẽ đóng vai trò là một **Trạm điều khiển GPIO linh hoạt**. Nó chỉ báo cáo cho Web biết nó HỖ TRỢ những LOẠI thiết bị nào. Khi lắp đèn mới vào mạch, không cần sửa C++ nữa!

### 0. Sinh Mã Định Danh (NODE_CODE) Tự Động Bằng Địa Chỉ MAC (BẮT BUỘC)

Tuyệt đối KHÔNG được code cứng tên mạch (kiểu `String NODE_CODE = "node_1";`) vì khi nạp code cho nhiều ESP32 khác nhau sẽ bị trùng lập và xung đột. Toàn phải dùng địa chỉ MAC Wifi vật lý của con chip.

```cpp
#include <WiFi.h>

String NODE_CODE;

void setup() {
    Serial.begin(115200);
    // ... Thực hiện kết nối WiFi ở đây ...

    // Đọc địa chỉ MAC vật lý của chip ESP32
    String mac = WiFi.macAddress(); 
    mac.replace(":", ""); // Xóa dấu hai chấm (VD: 3C:71:BF:42:00:11 -> 3C71BF420011)
    
    // Tạo mã NODE_CODE duy nhất toàn cầu
    NODE_CODE = "ESP_" + mac; 
    
    Serial.print("✅ MÃ NODE CỦA MẠCH NÀY LÀ (Gửi cho team Web): ");
    Serial.println(NODE_CODE); // Kết quả in ra: ESP_3C71BF420011
}
```

### 1. Báo cáo các LOẠI THIẾT BỊ hỗ trợ (Catalog)
Ngay khi kết nối MQTT thành công, ESP32 bắt buộc phải bắn danh sách các Khuôn mẫu phần cứng nó hỗ trợ. Không cần gán ID cụ thể.

```cpp
void publishCatalog() {
    String catalogTopic = String("hesta/nodes/") + NODE_CODE + "/catalog";
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();

    // 1. Khai báo hỗ trợ Đèn Relay
    JsonObject light = arr.createNestedObject();
    light["type"] = "LIGHT";
    light["label"] = "Đèn Relay (Cấu hình bằng Pin)";
    JsonObject lightCaps = light.createNestedObject("capabilities");
    JsonArray lightPower = lightCaps.createNestedArray("POWER");
    lightPower.add("TURN_ON");
    lightPower.add("TURN_OFF");
    lightPower.add("TOGGLE");

    // 2. Khai báo hỗ trợ Đèn LED RGB
    JsonObject rgb = arr.createNestedObject();
    rgb["type"] = "LED_RGB";
    rgb["label"] = "Đèn dây LED RGB";
    JsonObject rgbCaps = rgb.createNestedObject("capabilities");
    
    JsonArray rgbPower = rgbCaps.createNestedArray("POWER");
    rgbPower.add("TURN_ON");
    rgbPower.add("TURN_OFF");
    rgbPower.add("TOGGLE");
    
    JsonArray rgbColor = rgbCaps.createNestedArray("COLOR");
    rgbColor.add("SET_COLOR");
    
    JsonArray rgbBright = rgbCaps.createNestedArray("BRIGHTNESS");
    rgbBright.add("SET_BRIGHTNESS");

    // 3. Khai báo hỗ trợ Ổ cắm thông minh
    JsonObject plug = arr.createNestedObject();
    plug["type"] = "SMART_PLUG";
    plug["label"] = "Ổ cắm thông minh";
    JsonObject plugCaps = plug.createNestedObject("capabilities");
    JsonArray plugPower = plugCaps.createNestedArray("POWER");
    plugPower.add("TURN_ON");
    plugPower.add("TURN_OFF");
    plugPower.add("TOGGLE");

    // 4. Khai báo hỗ trợ Cảm biến nhiệt độ
    JsonObject dht = arr.createNestedObject();
    dht["type"] = "TEMP_HUMID_SENSOR";
    dht["label"] = "Cảm biến Nhiệt/Ẩm DHT22";
    JsonObject dhtCaps = dht.createNestedObject("capabilities");
    dhtCaps.createNestedArray("TEMPERATURE_READ");
    dhtCaps.createNestedArray("HUMIDITY_READ");

    // 5. Khai báo hỗ trợ Cảm biến chuyển động
    JsonObject pir = arr.createNestedObject();
    pir["type"] = "MOTION_SENSOR";
    pir["label"] = "Cảm biến Chuyển động PIR";
    JsonObject pirCaps = pir.createNestedObject("capabilities");
    pirCaps.createNestedArray("MOTION_DETECT");

    // 6. Khai báo hỗ trợ Cảm biến khói
    JsonObject mq2 = arr.createNestedObject();
    mq2["type"] = "SMOKE_SENSOR";
    mq2["label"] = "Cảm biến Khói MQ-2";
    JsonObject mq2Caps = mq2.createNestedObject("capabilities");
    mq2Caps.createNestedArray("SMOKE_DETECT");

    // 7. Khai báo hỗ trợ Cục phát hồng ngoại
    JsonObject ir = arr.createNestedObject();
    ir["type"] = "IR_REMOTE";
    ir["label"] = "Cục phát hồng ngoại IR";
    JsonObject irCaps = ir.createNestedObject("capabilities");
    JsonArray irTrans = irCaps.createNestedArray("IR_TRANSMIT");
    irTrans.add("SEND_IR_CODE");
    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(catalogTopic.c_str(), payload.c_str(), true); 
    Serial.println("Đã publish Catalog thành công!");
}
```

### 2. Hứng Lệnh Dựa Trên Chân GPIO (Đỉnh cao linh hoạt)

Vì Frontend App sẽ cho phép người dùng tự gõ số chân GPIO lúc họ ghép nối (Ví dụ họ gõ chân số 4). App sẽ tự động lưu thiết bị đó dưới dạng `local_id = pin_4`.
Nên khi nhận MQTT, Toàn chỉ cần cắt chuỗi `targetId` ("pin_4") ra để lấy đúng cái số 4 và gọi `digitalWrite(4, HIGH)`! 

```cpp
void onMessageCallback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, payload, length)) return;
    
    String commandId = doc["commandId"];
    String action = doc["action"];
    
    // Ví dụ lệnh Web bắn xuống: hesta/nodes/ESP_XXX/devices/pin_4/command
    String targetId = doc["target"]; // Kết quả: "pin_4"

    // Kiểm tra xem lệnh này có phải là lệnh điều khiển chân GPIO cho Relay/Đèn không
    if (targetId.startsWith("pin_")) {
        // Cắt chữ "pin_" đi, chỉ lấy số "4"
        int pinNumber = targetId.substring(4).toInt(); 
        
        // Cấu hình chân này là OUTPUT (Nếu trước đó chưa cấu hình)
        pinMode(pinNumber, OUTPUT);
        
        // Đọc trạng thái hiện tại
        int currentState = digitalRead(pinNumber); 
        String powerState = (currentState == HIGH) ? "ON" : "OFF";
        bool success = false;

        // XỬ LÝ LỆNH TƯƠNG ỨNG VỚI ACTION
        if (action == "TURN_ON") {
            digitalWrite(pinNumber, HIGH);
            powerState = "ON";
            success = true;
        } 
        else if (action == "TURN_OFF") {
            digitalWrite(pinNumber, LOW);
            powerState = "OFF";
            success = true;
        }
        else if (action == "TOGGLE") {
            digitalWrite(pinNumber, !currentState);
            powerState = (currentState == HIGH) ? "OFF" : "ON";
            success = true;
        }

        if (success) {
            // 1. Bắn ACK để App không bị treo
            String ackTopic = String("hesta/nodes/") + NODE_CODE + "/devices/" + targetId + "/ack";
            String ackPayload = "{\"commandId\": \"" + commandId + "\", \"status\": \"SUCCESS\"}";
            mqttClient.publish(ackTopic.c_str(), ackPayload.c_str());

            // 2. Bắn STATE để App cập nhật giao diện
            String stateTopic = String("hesta/nodes/") + NODE_CODE + "/devices/" + targetId + "/state";
            String statePayload = "{\"state\": {\"power\": \"" + powerState + "\"}}";
            mqttClient.publish(stateTopic.c_str(), statePayload.c_str(), true); 
        }
    } 
        // ==========================================
    // NẾU LÀ LỆNH ĐỔI MÀU LED RGB
    // ==========================================
    else if (action == "SET_COLOR") {
        // Lệnh có chứa tham số: {"action": "SET_COLOR", "parameters": {"r": 255, "g": 0, "b": 0}}
        int r = doc["parameters"]["r"];
        int g = doc["parameters"]["g"];
        int b = doc["parameters"]["b"];
        
        // (TUỲ VÀO LOẠI DÂY LED NHÓM MÌNH MUA MÀ TOÀN TỰ VIẾT CODE XUẤT MÀU NHÉ)
        // - Nếu là dây 4 chân (Analog): Dùng analogWrite(chân_R, r); cho 3 chân qua MOSFET.
        // - Nếu là dây 3 chân (NeoPixel/WS2812): Dùng thư viện Adafruit_NeoPixel.h chỉ xuất ra 1 chân Data.

        // Nhớ bắn ACK báo Web biết đã chạy xong
        String ackTopic = String("hesta/nodes/") + NODE_CODE + "/devices/" + targetId + "/ack";
        String ackPayload = "{\"commandId\": \"" + commandId + "\", \"status\": \"SUCCESS\"}";
        mqttClient.publish(ackTopic.c_str(), ackPayload.c_str());
    }
    // ==========================================
    // NẾU LÀ LỆNH PHÁT HỒNG NGOẠI (MÁY LẠNH/TV)
    // ==========================================
    else if (action == "SEND_IR_CODE") {
        // Lệnh chứa tham số: {"action": "SEND_IR_CODE", "parameters": {"protocol": "NEC", "code": "0x00FF00FF"}}
        String protocol = doc["parameters"]["protocol"].as<String>();
        String codeStr = doc["parameters"]["code"].as<String>();
        
        // Khởi tạo thư viện IRremote và phát mã (Giả sử chân IR cắm ở pin 18)
        // Nhớ khai báo IRsend irsend(18); ở đầu file
        unsigned long hexCode = strtoul(codeStr.c_str(), NULL, 16);
        irsend.sendNEC(hexCode, 32);

        String ackTopic = String("hesta/nodes/") + NODE_CODE + "/devices/" + targetId + "/ack";
        String ackPayload = "{\"commandId\": \"" + commandId + "\", \"status\": \"SUCCESS\"}";
        mqttClient.publish(ackTopic.c_str(), ackPayload.c_str());
    }
}
```

### 3. Tự Động Báo Cáo Dữ Liệu Lên Web (Dành Cho 3 Cảm Biến)

Các thiết bị như Cảm biến Nhiệt Ẩm (DHT22), Chuyển Động (PIR), và Khói (MQ-2) **KHÔNG BAO GIỜ nhận lệnh từ Web**. Chúng chỉ có nhiệm vụ thầm lặng đọc dữ liệu vật lý và tự động bắn (publish) trạng thái (STATE) lên Web.

Toàn Toàn phải viết các hàm này ở trong `void loop()`. Lưu ý, giả sử Toàn gắn DHT22 vào chân 14, PIR vào chân 12, thì `local_id` bắn lên phải trùng với số chân là `pin_14` và `pin_12`.

```cpp
unsigned long lastDHTTime = 0;
// Giả định: DHT22 cắm chân 14, PIR cắm chân 12, MQ2 cắm chân 13
int DHT_PIN = 14; 
int PIR_PIN = 12;
int MQ2_PIN = 13;

void loop() {
    mqttClient.loop();
    unsigned long currentMillis = millis();

    // ==========================================
    // 1. CẢM BIẾN NHIỆT ẨM DHT22 (Bắn 10 giây / lần)
    // ==========================================
    if (currentMillis - lastDHTTime >= 10000) {
        lastDHTTime = currentMillis;
        
        float t = 27.5; // dht.readTemperature();
        float h = 80.0; // dht.readHumidity();
        
        // Gửi lên Web theo cấu trúc "pin_14"
        String dhtTopic = String("hesta/nodes/") + NODE_CODE + "/devices/pin_" + String(DHT_PIN) + "/state";
        String dhtPayload = "{\"state\": {\"temperature\": " + String(t) + ", \"humidity\": " + String(h) + "}}";
        mqttClient.publish(dhtTopic.c_str(), dhtPayload.c_str(), true);
    }

    // ==========================================
    // 2. CẢM BIẾN CHUYỂN ĐỘNG PIR (Bắn NGAY LẬP TỨC khi có người đi qua)
    // ==========================================
    static int lastPirState = LOW;
    int currentPirState = digitalRead(PIR_PIN); 
    
    if (currentPirState != lastPirState) {
        lastPirState = currentPirState;
        
        String pirTopic = String("hesta/nodes/") + NODE_CODE + "/devices/pin_" + String(PIR_PIN) + "/state";
        String motionStatus = (currentPirState == HIGH) ? "true" : "false";
        String pirPayload = "{\"state\": {\"motion\": " + motionStatus + "}}";
        mqttClient.publish(pirTopic.c_str(), pirPayload.c_str(), true);
    }

    // ==========================================
    // 3. CẢM BIẾN KHÓI MQ-2 (Bắn NGAY LẬP TỨC khi phát hiện khói/cháy)
    // ==========================================
    static int lastSmokeState = LOW;
    int currentSmokeState = digitalRead(MQ2_PIN); // Hoặc đọc Analog tuỳ loại mạch 
    
    if (currentSmokeState != lastSmokeState) {
        lastSmokeState = currentSmokeState;
        
        String smokeTopic = String("hesta/nodes/") + NODE_CODE + "/devices/pin_" + String(MQ2_PIN) + "/state";
        // Giả sử mạch xuất HIGH khi có khói (Có mạch xuất LOW, Toàn tự đảo ngược lại nhé)
        String smokeStatus = (currentSmokeState == HIGH) ? "true" : "false"; 
        String smokePayload = "{\"state\": {\"smoke\": " + smokeStatus + "}}";
        mqttClient.publish(smokeTopic.c_str(), smokePayload.c_str(), true);
    }
}
```

---
---

# PHẦN II: TÍNH NĂNG ĐĂNG KÝ MẠCH (FULL-STACK BE & FE)

Vì Toàn chịu trách nhiệm luồng "Thêm Thiết Bị Mới", nên sau khi mạch C++ nhả ra được mã MAC (Ví dụ: `ESP_3C71BF420011`), Toàn phải code tiếp Backend và Frontend để hứng mã đó lưu vào Database.

## 1. Viết Code Backend (Java Spring Boot)
Toàn phải viết API để Web đẩy mã MAC xuống đăng ký hộ khẩu cho mạch.

**Bước 1.1: Tạo DTO Request**
`backend_hesta/src/main/java/com/hesta/backend/dto/request/NodeRegisterRequest.java`:
```java
package com.hesta.backend.dto.request;
import lombok.Data;
@Data
public class NodeRegisterRequest {
    private String nodeCode; // Truyền mã ESP_3C71BF420011 vào đây
}
```

**Bước 1.2: Viết hàm trong EdgeNodeService**
Trong `EdgeNodeServiceImpl.java`, viết hàm `registerNode`:
```java
@Transactional
public EdgeNodeResponse registerNode(UUID homeId, NodeRegisterRequest request) {
    Home home = homeRepository.findById(homeId).orElseThrow(() -> new RuntimeException("Home not found"));
    if (edgeNodeRepository.findByNodeCode(request.getNodeCode()).isPresent()) {
        throw new RuntimeException("Mạch này đã được đăng ký!");
    }

    EdgeNode newNode = EdgeNode.builder()
            .home(home)
            .nodeCode(request.getNodeCode())
            .status(EdgeNodeStatus.UNPAIRED) 
            .build();
    edgeNodeRepository.save(newNode);
    return null; // Tự map entity ra DTO nhé
}
```

**Bước 1.3: Mở API trong EdgeNodeController**
Trong `EdgeNodeController.java`, viết thêm endpoint POST:
```java
@PostMapping("/home/{homeId}")
public ResponseEntity<ApiResponse<EdgeNodeResponse>> registerNode(
        @PathVariable UUID homeId, 
        @RequestBody NodeRegisterRequest request) {
    EdgeNodeResponse node = edgeNodeService.registerNode(homeId, request);
    return ResponseEntity.ok(ApiResponse.<EdgeNodeResponse>builder().result(node).build());
}
```

## 2. Viết Code Frontend (React/TypeScript)

Toàn phải thiết kế giao diện để người dùng quét mã QR và đăng ký.

**Bước 2.1: Gọi API trong `deviceApi.ts`**
```typescript
export const registerEdgeNode = async (homeId: string, nodeCode: string): Promise<any> => {
  const response = await apiClient.post(`/nodes/home/${homeId}`, { nodeCode });
  return response.data.result;
};
```

**Bước 2.2: Vẽ Giao diện Quét QR (`AddNodeModal.tsx`)**
Dùng thư viện `react-qr-reader` để bật Camera điện thoại/laptop lên quét QR Code in trên hộp mạch.

```tsx
import { useState } from 'react';
import { QrReader } from 'react-qr-reader'; 
import { registerEdgeNode } from '../../services/deviceApi';

export function AddNodeModal({ homeId, onClose }) {
    const [isScanning, setIsScanning] = useState(true);

    const handleScan = async (result, error) => {
        if (result && isScanning) {
            setIsScanning(false); 
            const macCode = result?.text; 
            try {
                await registerEdgeNode(homeId, macCode);
                alert(`Quét thành công mã: ${macCode}. Vui lòng cắm điện để mạch kết nối!`);
                onClose();
            } catch (err) {
                alert("Lỗi: Mã QR không hợp lệ!");
                setIsScanning(true); 
            }
        }
    };

    return (
        <div className="modal">
            <h3>Thêm Bộ điều khiển (Quét QR)</h3>
            <div style={{ width: '300px', margin: '0 auto' }}>
                {isScanning ? (
                    <QrReader onResult={handleScan} constraints={{ facingMode: 'environment' }} />
                ) : (
                    <p className="text-green-500">Đang xử lý đăng ký...</p>
                )}
            </div>
            <button onClick={onClose}>Hủy bỏ</button>
        </div>
    );
}
```

---
---

# PHẦN III: TÍNH NĂNG THÊM THIẾT BỊ BẰNG CHÂN GPIO (PAIR & CONFIGURE)

Toàn Toàn lưu ý, thay vì hiển thị danh sách thiết bị cố định, giờ Dropdown trên App sẽ hiển thị CÁC LOẠI THIẾT BỊ mà mạch hỗ trợ. Người dùng phải nhập số Pin mà họ cắm mạch vào.

## 1. Viết Code Backend (Java API cho Thiết bị)

Toàn cần viết 2 API trong `DeviceController.java` và `DeviceServiceImpl` để phục vụ luồng này. Dưới đây là code mẫu chi tiết:

### Bước 1.1: Tạo DTO Request
Tạo file `backend_hesta/src/main/java/com/hesta/backend/dto/request/DeviceCreateRequest.java`:
```java
package com.hesta.backend.dto.request;
import lombok.Data;
import java.util.UUID;

@Data
public class DeviceCreateRequest {
    private UUID nodeId;
    private UUID roomId;
    private String name;
    private String deviceType;
    private String pinNumber;
}
```

### Bước 1.2: Viết hàm trong DeviceService & DeviceServiceImpl
Thêm 2 hàm này vào `DeviceService.java` (Interface) và `DeviceServiceImpl.java` (Class). *(Nhớ Inject `EdgeNodeRepository` và `RoomRepository` vào)*

```java
// Triển khai trong DeviceServiceImpl.java
@Override
public List<DeviceResponse> getDevicesByNode(UUID userId, UUID nodeId) {
    return deviceRepository.findByNodeId(nodeId).stream()
            .map(DeviceResponse::fromEntity)
            .collect(Collectors.toList());
}

@Override
@Transactional
public DeviceResponse createDevice(UUID userId, DeviceCreateRequest request) {
    // 1. Kiểm tra mạch
    com.hesta.backend.entity.EdgeNode node = edgeNodeRepository.findById(request.getNodeId())
            .orElseThrow(() -> new RuntimeException("Node not found"));
            
    // 2. Kiểm tra phòng
    com.hesta.backend.entity.Room room = roomRepository.findById(request.getRoomId())
            .orElseThrow(() -> new RuntimeException("Room not found"));

    // 3. Tìm năng lực (Capabilities) từ Catalog
    java.util.List<java.util.Map<String, Object>> catalog = node.getSupportedTypes();
    java.util.Map<String, java.util.List<String>> capabilities = null;
    if (catalog != null) {
        for (java.util.Map<String, Object> typeObj : catalog) {
            if (request.getDeviceType().equals(typeObj.get("type"))) {
                capabilities = (java.util.Map<String, java.util.List<String>>) typeObj.get("capabilities");
                break;
            }
        }
    }

    // 4. Tạo thiết bị mới với local_id là số chân GPIO (VD: pin_4)
    com.hesta.backend.entity.Device newDevice = com.hesta.backend.entity.Device.builder()
            .node(node)
            .room(room)
            .name(request.getName())
            .deviceType(request.getDeviceType())
            .localId("pin_" + request.getPinNumber())
            .capabilities(capabilities)
            .build();

    deviceRepository.save(newDevice);

    // 5. Đổi trạng thái mạch sang ONLINE
    if (node.getStatus() == com.hesta.backend.enums.EdgeNodeStatus.UNPAIRED) {
        node.setStatus(com.hesta.backend.enums.EdgeNodeStatus.ONLINE);
        edgeNodeRepository.save(node);
    }

    return DeviceResponse.fromEntity(newDevice);
}
```

### Bước 1.3: Thêm API vào Controller
Mở `EdgeNodeController.java` (để lấy danh sách thiết bị) và `DeviceController.java` (để tạo thiết bị mới).

**Trong `EdgeNodeController.java`:** *(Nhớ Inject `DeviceService` vào nhé)*
```java
@GetMapping("/{nodeId}/devices")
public ResponseEntity<ApiResponse<List<DeviceResponse>>> getDevicesByNode(@PathVariable UUID nodeId) {
    UUID dummyUserId = UUID.randomUUID(); // Tự sửa lại lấy User ID từ SecurityContext nhé
    List<DeviceResponse> devices = deviceService.getDevicesByNode(dummyUserId, nodeId);
    return ResponseEntity.ok(ApiResponse.<List<DeviceResponse>>builder().result(devices).build());
}
```

**Trong `DeviceController.java`:**
```java
@PostMapping
public ResponseEntity<ApiResponse<DeviceResponse>> createDevice(@RequestBody DeviceCreateRequest request) {
    UUID dummyUserId = UUID.randomUUID(); // Tự sửa lại lấy User ID từ SecurityContext nhé
    DeviceResponse device = deviceService.createDevice(dummyUserId, request);
    return ResponseEntity.ok(ApiResponse.<DeviceResponse>builder().result(device).build());
}
```

## 2. Viết Code Frontend (React/TypeScript)

Nhiệm vụ của Toàn ở Frontend là vẽ một cái giao diện (Trang `PairDevicePage.tsx`) làm 2 việc:
1. Gọi API `GET /api/v1/nodes/{nodeId}` của `EdgeNodeController` để lấy về mảng `supportedTypes` (chính là Catalog Toàn bắn từ ESP32).
2. Vẽ Dropdown chọn Loại. Yêu cầu nhập Tên và Chọn Số chân GPIO.

**Sườn Giao diện Ghép nối:**
```tsx
import { useEffect, useState } from 'react';
import { getSupportedTypes, createDevice } from '../../services/deviceApi';

export function AddDeviceByPinModal({ nodeId, roomId, onClose }) {
    const [types, setTypes] = useState<any[]>([]);
    
    // Form data
    const [selectedType, setSelectedType] = useState("");
    const [deviceName, setDeviceName] = useState("");
    const [pinNumber, setPinNumber] = useState("");
    
    // Danh sách các chân GPIO phổ biến của ESP32 có thể dùng làm Output
    const ALL_ESP32_PINS = [2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33];
    const [usedPins, setUsedPins] = useState<number[]>([]);

    useEffect(() => {
        // 1. Tải các Type mà ESP32 hỗ trợ từ Catalog
        getSupportedTypes(nodeId).then(data => {
            setTypes(data);
            if (data.length > 0) setSelectedType(data[0].type);
        });
        
        // 2. Tải danh sách thiết bị ĐÃ CÓ của Node này để lọc ra các chân đã bị chiếm (local_id = pin_X)
        // Gọi API GET /api/v1/nodes/{nodeId}/devices Toàn vừa viết ở trên
        getDevicesOfNode(nodeId).then(devices => {
            const taken = devices
                .map(d => d.localId)
                .filter(id => id.startsWith("pin_"))
                .map(id => parseInt(id.replace("pin_", "")));
            setUsedPins(taken);
            
            // Tự động chọn chân trống đầu tiên
            const firstAvailable = ALL_ESP32_PINS.find(p => !taken.includes(p));
            if (firstAvailable) setPinNumber(firstAvailable.toString());
        });
    }, [nodeId]);

    const handleSave = async () => {
        try {
            await createDevice(nodeId, roomId, selectedType, deviceName, pinNumber);
            alert("Thêm thiết bị thành công!");
            onClose();
        } catch (error) {
            alert("Lỗi khi thêm thiết bị!");
        }
    };

    return (
        <div className="modal">
            <h3>Thêm Thiết bị mới vào Mạch</h3>
            
            <label>Chọn loại thiết bị:</label>
            <select value={selectedType} onChange={e => setSelectedType(e.target.value)}>
                {types.map(t => (
                    <option key={t.type} value={t.type}>{t.label} ({t.type})</option>
                ))}
            </select>

            <label>Tên thiết bị (VD: Đèn trần phòng khách):</label>
            <input value={deviceName} onChange={e => setDeviceName(e.target.value)} />

            <label>Cắm vào chân GPIO số mấy trên ESP32?</label>
            <select value={pinNumber} onChange={e => setPinNumber(e.target.value)}>
                {ALL_ESP32_PINS.map(pin => {
                    const isTaken = usedPins.includes(pin);
                    return (
                        <option key={pin} value={pin} disabled={isTaken}>
                            Chân số {pin} {isTaken ? "(Đã sử dụng)" : "(Còn trống)"}
                        </option>
                    );
                })}
            </select>

            <button onClick={handleSave}>Lưu thiết bị</button>
        </div>
    );
}
```
Như vậy là luồng đi dọc hệ thống (Vertical Slicing) của Toàn đã chính thức hoàn thiện 100%!

---

## PHẦN IV: CÔNG CỤ TEST ĐỘC LẬP (MOCK TEST) - DÀNH RIÊNG CHO TEAM IoT

Trong thời gian chờ Team Web/App hoàn thiện giao diện (FE) và Backend (BE), Toàn có thể tự test và nghiệm thu 100% code C++ của mình bằng 2 file Python giả lập mà Kiến trúc sư đã chuẩn bị sẵn trong thư mục `iot_hesta/test/`.

### 1. Chuẩn bị môi trường
- Đảm bảo Toàn đã cài Python trên máy tính.
- Mở Terminal/CMD, cd vào thư mục `iot_hesta/test/` và cài thư viện MQTT:
  ```bash
  pip install -r requirements.txt
  ```

### 2. Dùng `test_mqtt_sensors.py` (Giả lập Backend lắng nghe Cảm biến)
- Cắm mạch ESP32 vào nguồn, mở 1 cửa sổ Terminal mới và chạy lệnh:
  ```bash
  python test_mqtt_sensors.py
  ```
- **Tác dụng:** Tool sẽ liên tục lắng nghe và in ra màn hình những dữ liệu mà mạch của Toàn tự động bắn lên. 
- **Cách test:** 
  1. Bấm nút Reset trên mạch ESP32 -> Xem Tool có in ra mảng JSON `Catalog` không.
  2. Quẹt tay qua cảm biến PIR hoặc xịt khói vào MQ-2 -> Xem Tool có in ra báo động `"motion": true` hoặc `"smoke": true` ngay lập tức không.
  3. Ngồi nhìn màn hình -> Xem 10 giây 1 lần mạch có đều đặn báo cáo Nhiệt độ / Độ ẩm lên không.

### 3. Dùng `test_mqtt_commands.py` (Giả lập Người dùng bấm nút trên Web/App)
- Mở thêm 1 cửa sổ Terminal thứ 2, chạy lệnh:
  ```bash
  python test_mqtt_commands.py
  ```
- **Tác dụng:** Hiện ra một Menu tương tác cho Toàn đóng vai người dùng bấm nút trên App.
- **Cách test:**
  1. Gõ mã `NODE_CODE` của mạch Toàn (VD: `ESP_3C71BF420011`).
  2. Gõ số 1 để giả vờ bấm nút "Bật Đèn" trên UI.
  3. Quan sát đèn LED / Relay trên mạch xem có sáng không.
  4. Quan sát trên Terminal xem mạch của Toàn có nhả gói `ACK` (`"status": "SUCCESS"`) về báo cáo thành công hay không.
  5. Tiếp tục chọn thử các lệnh Đổi màu RGB hoặc Phát Hồng ngoại để nghiệm thu.

> **Chốt lại:** Nếu Toàn chạy 2 tool này mượt mà, LED sáng, Relay cạch cạch, Log nhảy ầm ầm đúng chuẩn JSON... thì phần IoT của Toàn coi như đã **PASS 100%**, có thể tự tin đi ngủ chờ Web ráp vào thôi!
