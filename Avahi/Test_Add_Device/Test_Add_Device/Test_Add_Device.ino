#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =============================================================================
// TEST ADD DEVICE - SLIDING GATE MOTOR (L298N)
// Tích hợp quy trình Add Device (BLE Provisioning -> mDNS Discovery -> MQTT Onboarding)
// dựa trên Avahi_Linux.ino và điều khiển cổng tự động từ Control_Gate_Motor_BLE_mDNS.ino
//
// SƠ ĐỒ NỐI DÂY PHẦN CỨNG (L298N + LIMIT SWITCHES)
// -----------------------------------------------------------------------------
// ESP32 / ESP32-CAM / ESP32-S3:
// GPIO13 -> L298N IN1 (Motor Direction A)
// GPIO14 -> L298N IN2 (Motor Direction B)
// GPIO12 -> L298N ENA (PWM Speed Control)
//
// GPIO15 -> Công tắc hành trình MỞ (LIMIT OPEN - INPUT_PULLUP)
// GPIO2  -> Công tắc hành trình ĐÓNG (LIMIT CLOSE - INPUT_PULLUP)
// =============================================================================

// =============================================================================
// CẤU HÌNH BLE PROVISIONING (ĐỒNG BỘ VỚI APP SYNA)
// =============================================================================
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789000"
#define COMM_CHAR_UUID      "12345678-1234-1234-1234-123456789001"

// Tên thiết bị quảng bá qua BLE (App quét tìm theo tiền tố 'SYNA' hoặc 'ESP32')
#define DEFAULT_DEVICE_NAME "SYNA-ESP32S3"
#define FIRMWARE_VERSION    "1.0.0"

// =============================================================================
// CẤU HÌNH NODE & DEVICE ID (ĐỂ TRỐNG ĐỂ TỰ ĐIỀN BẰNG TAY)
// =============================================================================
// Node ID của ESP32 (để trống "" để tự thêm tay vào sau, ví dụ: "esp32-001" hoặc UUID)
#define DEFAULT_NODE_ID     ""

// Device ID của cổng (để trống "" để tự thêm tay ID mới vào sau, ví dụ: "69033e9f-0c68-4548-9957-1fd1bde798cd")
#define GATE_DEVICE_ID      ""

// Device ID cũ để dự phòng test song song (nếu cần test đồng thời thiết bị cũ 2d566d0a...)
#define LEGACY_GATE_DEVICE_ID "2d566d0a-f7d6-4fac-a0bf-18912da4ab28"
#define LEGACY_NODE_ID        "8d1cdd82-b339-469e-be13-7e91070f7ae5"

// Fallback MQTT broker IP (khi mDNS không tìm thấy hoặc chạy offline)
#define FALLBACK_MQTT_IP    "192.168.137.107"
#define FALLBACK_MQTT_PORT  1883

// =============================================================================
// CẤU HÌNH CHÂN ĐIỀU KHIỂN MOTOR L298N & LIMIT SWITCH
// =============================================================================
#define MOTOR_IN1           13
#define MOTOR_IN2           14
#define MOTOR_ENA           12

#define LIMIT_OPEN          15
#define LIMIT_CLOSE          2

#define MOTOR_SPEED         200  // Tốc độ PWM (0 - 255)
#define LIMIT_PRESSED       HIGH // Logic khi chạm công tắc hành trình

// =============================================================================
// TRẠNG THÁI CỔNG
// =============================================================================
enum GateState {
  GATE_STOPPED,
  GATE_OPENING,
  GATE_CLOSING,
  GATE_OPEN,
  GATE_CLOSED
};

GateState gateState = GATE_STOPPED;

// =============================================================================
// BIẾN TOÀN CỤC (GLOBAL VARIABLES)
// =============================================================================
Preferences preferences;

BLEServer *bleServer = nullptr;
BLECharacteristic *commCharacteristic = nullptr;

bool bleClientConnected = false;
bool shouldConnectWiFi = false;

String wifiSSID = "";
String wifiPassword = "";
String currentNodeId = DEFAULT_NODE_ID;
String currentDeviceId = GATE_DEVICE_ID;

IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = FALLBACK_MQTT_PORT;
bool mqttBrokerFound = false;
bool mdnsStarted = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL = 30000; // 30s gửi telemetry 1 lần

// =============================================================================
// HÀM KHỞI TẠO & ĐIỀU KHIỂN PWM CHO MOTOR (TƯƠNG THÍCH CORE ESP32 V2.X & V3.X)
// =============================================================================
void setupPWM() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(MOTOR_ENA, 1000, 8);
#else
  ledcSetup(0, 1000, 8);
  ledcAttachPin(MOTOR_ENA, 0);
#endif
  Serial.println("[PWM] Da khoi tao LEDC PWM cho ENA (GPIO 12)");
}

void writeMotorPWM(uint32_t speed) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(MOTOR_ENA, speed);
#else
  ledcWrite(0, speed);
#endif
}

// =============================================================================
// CHUYỂN ĐỔI TRẠNG THÁI CỔNG THÀNH CHUỖI
// =============================================================================
const char* gateStateToString() {
  switch (gateState) {
    case GATE_STOPPED: return "STOPPED";
    case GATE_OPENING: return "OPENING";
    case GATE_CLOSING: return "CLOSING";
    case GATE_OPEN:    return "OPEN";
    case GATE_CLOSED:  return "CLOSED";
    default:           return "UNKNOWN";
  }
}

// Forward declarations
void publishState();
void publishTelemetry();
void publishAck(const char* action, const char* status);
bool connectWiFi();
bool discoverMQTT();
bool connectAndPublishMQTT();

// =============================================================================
// HELPER: Gửi thông báo trạng thái về App qua BLE Notify (JSON)
// Đáp ứng chính xác các trạng thái mà App SYNA (Flutter) lắng nghe:
// - ble_connected, connecting_wifi, wifi_connected, wifi_failed,
// - mdns_discovered, mqtt_connected, ready, mqtt_failed
// =============================================================================
void notifyBleStatus(const char *status, const char *key1 = nullptr, const char *val1 = nullptr, const char *key2 = nullptr, const char *val2 = nullptr) {
  if (!bleClientConnected || commCharacteristic == nullptr) {
    return;
  }

  JsonDocument doc;
  doc["status"] = status;
  if (key1 && val1) {
    doc[key1] = val1;
  }
  if (key2 && val2) {
    doc[key2] = val2;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);

  commCharacteristic->setValue(jsonStr.c_str());
  commCharacteristic->notify();
  delay(50);

  Serial.print("[BLE NOTIFY -> APP] ");
  Serial.println(jsonStr);
}

// =============================================================================
// ĐIỀU KHIỂN MOTOR (STOP, OPEN, CLOSE)
// =============================================================================

void motorStop(bool publishMqtt = true) {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  writeMotorPWM(0);

  gateState = GATE_STOPPED;
  Serial.println("[MOTOR] DUNG MOTOR (GATE_STOPPED)");

  if (publishMqtt) {
    publishState();
  }
}

void motorOpen() {
  if (digitalRead(LIMIT_OPEN) == LIMIT_PRESSED) {
    Serial.println("[MOTOR] Cong da mo hoan toan (Limit Open dang cham)!");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    writeMotorPWM(0);
    gateState = GATE_OPEN;
    publishState();
    return;
  }

  Serial.println("[MOTOR] DANG MO CONG (GATE_OPENING)...");
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  writeMotorPWM(MOTOR_SPEED);

  gateState = GATE_OPENING;
  publishState();
}

void motorClose() {
  if (digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED) {
    Serial.println("[MOTOR] Cong da dong hoan toan (Limit Close dang cham)!");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    writeMotorPWM(0);
    gateState = GATE_CLOSED;
    publishState();
    return;
  }

  Serial.println("[MOTOR] DANG DONG CONG (GATE_CLOSING)...");
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
  writeMotorPWM(MOTOR_SPEED);

  gateState = GATE_CLOSING;
  publishState();
}

// =============================================================================
// KIỂM TRA CÔNG TẮC HÀNH TRÌNH (LIMIT SWITCHES)
// =============================================================================
void checkLimitSwitch() {
  bool openPressed = (digitalRead(LIMIT_OPEN) == LIMIT_PRESSED);
  bool closePressed = (digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED);

  if (gateState == GATE_OPENING && openPressed) {
    Serial.println();
    Serial.println("======================================");
    Serial.println("[LIMIT] CHAM CONG TAC HANH TRINH MO -> MO XONG");
    Serial.println("======================================");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    writeMotorPWM(0);
    gateState = GATE_OPEN;
    publishState();
  }

  if (gateState == GATE_CLOSING && closePressed) {
    Serial.println();
    Serial.println("======================================");
    Serial.println("[LIMIT] CHAM CONG TAC HANH TRINH DONG -> DONG XONG");
    Serial.println("======================================");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    writeMotorPWM(0);
    gateState = GATE_CLOSED;
    publishState();
  }
}

void printLimitStatus() {
  Serial.print("LIMIT OPEN: ");
  Serial.print((digitalRead(LIMIT_OPEN) == LIMIT_PRESSED) ? "PRESSED" : "NOT PRESSED");
  Serial.print(" | LIMIT CLOSE: ");
  Serial.println((digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED) ? "PRESSED" : "NOT PRESSED");
}

void printGateStatus() {
  Serial.print("[GATE STATUS] Trạng thái cổng: ");
  Serial.println(gateStateToString());
  printLimitStatus();
}

// =============================================================================
// LƯU TRỮ VÀ NẠP CẤU HÌNH TỪ BỘ NHỚ FLASH (PREFERENCES NVS)
// =============================================================================
void loadStoredConfig() {
  preferences.begin("syna_cfg", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");

  // Ưu tiên giá trị người dùng điền tay vào #define
  if (String(DEFAULT_NODE_ID).length() > 0) {
    currentNodeId = DEFAULT_NODE_ID;
  } else {
    currentNodeId = preferences.getString("node_id", "esp32-001");
    if (currentNodeId.length() == 0) currentNodeId = "esp32-001";
  }

  if (String(GATE_DEVICE_ID).length() > 0) {
    currentDeviceId = GATE_DEVICE_ID;
  } else {
    currentDeviceId = preferences.getString("device_id", "");
  }

  String storedMqttHost = preferences.getString("mqtt_host", "");
  uint16_t storedMqttPort = preferences.getUShort("mqtt_port", 0);
  preferences.end();

  if (wifiSSID.length() > 0) {
    Serial.println("[NVS] Tim thay WiFi trong Flash: " + wifiSSID);
  } else {
    Serial.println("[NVS] Chua co WiFi trong Flash. Cho Provision qua BLE...");
  }

  Serial.println("[CONFIG] Current Node ID  : " + currentNodeId);
  if (currentDeviceId.length() > 0) {
    Serial.println("[CONFIG] Current Device ID: " + currentDeviceId);
  } else {
    Serial.println("[CONFIG] Current Device ID: (DANG DE TRONG \"\" - San sang tu dong bat qua MQTT Wildcard hoac de ban dien tay vao code)");
  }

  if (storedMqttHost.length() > 0 && storedMqttPort > 0) {
    if (mqttBrokerIP.fromString(storedMqttHost)) {
      mqttBrokerPort = storedMqttPort;
      mqttBrokerFound = true;
      Serial.printf("[NVS] Tim thay MQTT Broker trong Flash: %s:%d\n", storedMqttHost.c_str(), storedMqttPort);
    }
  }
}

void saveStoredWiFi(const String& ssid, const String& password) {
  preferences.begin("syna_cfg", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  Serial.println("[NVS] Da luu cau hinh WiFi vao Flash NVS.");
}

void saveStoredMQTT(const String& host, uint16_t port) {
  preferences.begin("syna_cfg", false);
  preferences.putString("mqtt_host", host);
  preferences.putUShort("mqtt_port", port);
  preferences.end();
  Serial.println("[NVS] Da luu MQTT Broker vao Flash NVS.");
}

void clearStoredConfig() {
  preferences.begin("syna_cfg", false);
  preferences.clear();
  preferences.end();
  mqttBrokerFound = false;
  Serial.println("[NVS] Da xoa toan bo cau hinh WiFi & MQTT trong Flash.");
}

// =============================================================================
// BLE CALLBACKS
// =============================================================================
class GateBLEServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
    Serial.println();
    Serial.println("==================================================");
    Serial.println("[BLE] CLIENT CONNECTED (Dien thoai / App SYNA)");
    Serial.println("==================================================");
    notifyBleStatus("ble_connected", "device", DEFAULT_DEVICE_NAME);
  }

  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;
    Serial.println();
    Serial.println("[BLE] CLIENT DISCONNECTED -> Tiep tuc phat quang ba (Advertising)");
    BLEDevice::startAdvertising();
  }
};

class GateBLECharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String payload = characteristic->getValue().c_str();
    payload.trim();

    Serial.println();
    Serial.println("==================================================");
    Serial.print("[BLE RX] Nhan goi tin tu App: ");
    Serial.println(payload);
    Serial.println("==================================================");

    if (payload.length() == 0) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[BLE ERROR] deserializeJson() failed: ");
      Serial.println(error.c_str());
      notifyBleStatus("error", "message", "invalid_json_format");
      return;
    }

    const char *action = doc["action"] | "";

    // Hành động 1: Cấu hình Wi-Fi và Onboarding Thiết bị
    if (strcmp(action, "set_wifi") == 0) {
      wifiSSID = (const char*)(doc["ssid"] | "");
      wifiPassword = (const char*)(doc["password"] | "");

      if (wifiSSID.length() == 0) {
        Serial.println("[BLE ERROR] SSID rong!");
        notifyBleStatus("wifi_failed", "error", "empty_ssid");
        return;
      }

      // Hỗ trợ cập nhật Node ID / Device ID nếu App gửi kèm
      if (doc.containsKey("node_id")) {
        currentNodeId = (const char*)(doc["node_id"] | DEFAULT_NODE_ID);
      }
      if (doc.containsKey("device_id")) {
        currentDeviceId = (const char*)(doc["device_id"] | GATE_DEVICE_ID);
      }

      // Hỗ trợ truyền thẳng MQTT IP từ App nếu có
      if (doc.containsKey("mqtt_host")) {
        const char* mHost = doc["mqtt_host"] | "";
        int mPort = doc["mqtt_port"] | FALLBACK_MQTT_PORT;
        if (strlen(mHost) > 0 && mqttBrokerIP.fromString(mHost)) {
          mqttBrokerPort = mPort;
          mqttBrokerFound = true;
          saveStoredMQTT(mHost, mPort);
        }
      }

      Serial.println("[BLE] Da nhan thong tin Wi-Fi hop le:");
      Serial.println(" - SSID: " + wifiSSID);
      Serial.printf(" - Do dai mat khau: %d\n", wifiPassword.length());

      // Phản hồi ngay cho App: Đang bắt đầu kết nối Wi-Fi
      notifyBleStatus("connecting_wifi", "ssid", wifiSSID.c_str());
      shouldConnectWiFi = true;
    }
    // Hành động 2: Reset cấu hình Wi-Fi & MQTT
    else if (strcmp(action, "reset_wifi") == 0) {
      clearStoredConfig();
      wifiSSID = "";
      wifiPassword = "";
      WiFi.disconnect(true);
      mqttClient.disconnect();
      notifyBleStatus("wifi_reset", "message", "WiFi credentials cleared");
      Serial.println("[BLE] Da reset thong tin WiFi theo yeu cau tu App.");
    }
    // Hành động 3: Lấy thông tin thiết bị (get_info)
    else if (strcmp(action, "get_info") == 0) {
      JsonDocument info;
      info["status"] = "info";
      info["nodeId"] = currentNodeId;
      info["deviceId"] = currentDeviceId;
      info["device"] = "gate";
      info["state"] = gateStateToString();
      info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      info["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
      info["mqtt_connected"] = mqttClient.connected();
      String out;
      serializeJson(info, out);
      characteristic->setValue(out.c_str());
      characteristic->notify();
    }
    else {
      Serial.print("[BLE WARN] Unknown action: ");
      Serial.println(action);
      notifyBleStatus("error", "message", "unknown_action");
    }
  }
};

// =============================================================================
// KHỞI ĐỘNG DỊCH VỤ BLE
// =============================================================================
void startBLE() {
  Serial.printf("Khoi tao BLE Device [%s]...\n", DEFAULT_DEVICE_NAME);

  BLEDevice::init(DEFAULT_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new GateBLEServerCallbacks());

  BLEService *service = bleServer->createService(SERVICE_UUID);

  commCharacteristic = service->createCharacteristic(
    COMM_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  commCharacteristic->setCallbacks(new GateBLECharacteristicCallbacks());
  commCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();

  // Đặt tên thiết bị vào gói quảng bá chính
  BLEAdvertisementData advData;
  advData.setName(DEFAULT_DEVICE_NAME);
  advData.setFlags(0x06); // General Discoverable Mode, BR/EDR not supported

  // Đặt Service UUID vào gói phản hồi quét (Scan Response Packet)
  BLEAdvertisementData scanRespData;
  scanRespData.setCompleteServices(BLEUUID(SERVICE_UUID));

  advertising->setAdvertisementData(advData);
  advertising->setScanResponseData(scanRespData);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("==================================================");
  Serial.printf("BLE Da san sang! Quang ba voi ten: %s\n", DEFAULT_DEVICE_NAME);
  Serial.printf("Service UUID: %s\n", SERVICE_UUID);
  Serial.printf("Comm UUID:    %s\n", COMM_CHAR_UUID);
  Serial.println("==================================================");
}

// =============================================================================
// KẾT NỐI WIFI
// =============================================================================
bool connectWiFi() {
  if (wifiSSID.isEmpty()) {
    Serial.println("[WiFi] Chua co SSID!");
    return false;
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.println("BAT DAU KET NOI WIFI...");
  Serial.println("SSID: " + wifiSSID);
  Serial.println("==================================================");

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");

    wl_status_t st = WiFi.status();
    if (st == WL_CONNECT_FAILED) {
      Serial.println("\n[ERROR] WiFi auth failed (Sai mat khau WiFi)!");
      notifyBleStatus("wifi_failed", "error", "sai_mat_khau_wifi");
      return false;
    }
    if (st == WL_NO_SSID_AVAIL) {
      Serial.println("\n[ERROR] WiFi SSID not found (Khong tim thay SSID)!");
      notifyBleStatus("wifi_failed", "error", "khong_tim_thay_ssid");
      return false;
    }

    // Timeout 18 giay
    if (millis() - startTime > 18000) {
      Serial.println("\n[ERROR] WiFi connection timeout (Sai mat khau hoac song yeu)!");
      notifyBleStatus("wifi_failed", "error", "timeout_sai_mat_khau");
      return false;
    }
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.println("KET NOI WIFI THANH CONG!");
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  Serial.print("Gateway:    "); Serial.println(WiFi.gatewayIP());
  Serial.print("RSSI:       "); Serial.println(WiFi.RSSI());
  Serial.println("==================================================");

  // Lưu thông tin Wi-Fi vào Flash NVS
  saveStoredWiFi(wifiSSID, wifiPassword);

  // Báo thành công về App qua BLE cùng IP nhận được
  notifyBleStatus("wifi_connected", "ip", WiFi.localIP().toString().c_str());
  mdnsStarted = false;
  return true;
}

// =============================================================================
// TÌM KIẾM MQTT BROKER QUA MDNS (DNS-SD) TƯƠNG THÍCH AVAHI LINUX
// =============================================================================
bool discoverMQTT() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println("DANG TIM KIEM MQTT BROKER QUA MDNS...");
  Serial.println("Service: _syna-mqtt._tcp");
  Serial.println("==================================================");

  if (!mdnsStarted) {
    if (!MDNS.begin("syna-gate-node")) {
      Serial.println("[ERROR] Failed to start mDNS responder");
    }
    mdnsStarted = true;
  }

  int count = MDNS.queryService("syna-mqtt", "tcp");

  if (count <= 0) {
    Serial.println("[WARN] Khong tim thay dich vu _syna-mqtt._tcp qua mDNS.");

    // Fallback 1: Địa chỉ IP đã lưu trong Flash NVS
    if (mqttBrokerFound && mqttBrokerIP != IPAddress(0, 0, 0, 0)) {
      Serial.printf("[FALLBACK 1] Su dung MQTT Broker da luu trong Flash: %s:%d\n",
                    mqttBrokerIP.toString().c_str(), mqttBrokerPort);
      return true;
    }

    // Fallback 2: IP cau hinh san (FALLBACK_MQTT_IP)
    if (mqttBrokerIP.fromString(FALLBACK_MQTT_IP)) {
      mqttBrokerPort = FALLBACK_MQTT_PORT;
      mqttBrokerFound = true;
      Serial.println("[FALLBACK 2] Dung IP cau hinh san: " FALLBACK_MQTT_IP);
      return true;
    }

    // Fallback 3: Lay dia chi Gateway IP
    IPAddress gw = WiFi.gatewayIP();
    if (gw != IPAddress(0, 0, 0, 0)) {
      mqttBrokerIP = gw;
      mqttBrokerPort = FALLBACK_MQTT_PORT;
      mqttBrokerFound = true;
      Serial.println("[FALLBACK 3] Dung dia chi Gateway IP: " + gw.toString());
      return true;
    }

    return false;
  }

  Serial.printf("Tim thay %d MQTT service(s) qua mDNS!\n", count);
  mqttBrokerIP = MDNS.address(0);
  mqttBrokerPort = MDNS.port(0);
  mqttBrokerFound = true;

  Serial.println("--------------------------------------------------");
  Serial.print("Broker IP:   "); Serial.println(mqttBrokerIP);
  Serial.print("Broker Port: "); Serial.println(mqttBrokerPort);
  Serial.println("--------------------------------------------------");

  saveStoredMQTT(mqttBrokerIP.toString(), mqttBrokerPort);
  notifyBleStatus("mdns_discovered", "broker", mqttBrokerIP.toString().c_str(), "port", String(mqttBrokerPort).c_str());
  return true;
}

// =============================================================================
// MQTT PUBLISHERS (DISCOVERY, STATE, TELEMETRY, ACK)
// =============================================================================

void publishState() {
  if (!mqttClient.connected()) return;

  bool openLimit = (digitalRead(LIMIT_OPEN) == LIMIT_PRESSED);
  bool closeLimit = (digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED);

  JsonDocument doc;
  doc["nodeId"] = currentNodeId;
  doc["deviceId"] = currentDeviceId;
  doc["device"] = "gate";
  doc["state"] = gateStateToString();
  doc["limit_open"] = openLimit;
  doc["limit_close"] = closeLimit;

  String payload;
  serializeJson(doc, payload);

  // 1. Publish tới topic Device cụ thể nếu đã có ID
  if (currentDeviceId.length() > 0) {
    String stateTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/state";
    mqttClient.publish(stateTopic.c_str(), payload.c_str(), true);
  } else {
    Serial.println("[MQTT STATE NOTICE] currentDeviceId dang de trong. Hay dien ID vao #define GATE_DEVICE_ID neu muon publish chinh xac.");
  }

  // 2. Publish tới topic cấp Node
  String nodeStateTopic = "hesta/nodes/" + currentNodeId + "/state";
  mqttClient.publish(nodeStateTopic.c_str(), payload.c_str(), true);

  // 3. Publish tới topic legacy nếu cần
  if (String(LEGACY_NODE_ID).length() > 0 && currentNodeId != LEGACY_NODE_ID && currentDeviceId.length() > 0) {
    String legacyTopic = "hesta/nodes/" + String(LEGACY_NODE_ID) + "/devices/" + currentDeviceId + "/state";
    mqttClient.publish(legacyTopic.c_str(), payload.c_str(), true);
  }

  Serial.print("[MQTT PUB STATE] -> ");
  Serial.println(payload);
}

void publishTelemetry() {
  if (!mqttClient.connected()) return;

  bool openLimit = (digitalRead(LIMIT_OPEN) == LIMIT_PRESSED);
  bool closeLimit = (digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED);

  JsonDocument doc;
  doc["nodeId"] = currentNodeId;
  doc["deviceId"] = currentDeviceId;
  doc["device"] = "gate";
  doc["state"] = gateStateToString();
  doc["limit_open"] = openLimit;
  doc["limit_close"] = closeLimit;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;

  String payload;
  serializeJson(doc, payload);

  if (currentDeviceId.length() > 0) {
    String telemTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/telemetry";
    mqttClient.publish(telemTopic.c_str(), payload.c_str());
  }
}

void publishAck(const char* action, const char* status) {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["deviceId"] = currentDeviceId;
  doc["action"] = action;
  doc["status"] = status;

  String payload;
  serializeJson(doc, payload);

  if (currentDeviceId.length() > 0) {
    String ackTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/ack";
    mqttClient.publish(ackTopic.c_str(), payload.c_str());
  }

  Serial.print("[MQTT PUB ACK] -> ");
  Serial.println(payload);
}

// =============================================================================
// MQTT MESSAGE CALLBACK (XỬ LÝ LỆNH TỪ BACKEND / APP)
// =============================================================================
void mqttCallback(char* topic, byte* payloadBytes, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payloadBytes[i];
  }
  message.trim();

  Serial.println();
  Serial.println("==================================================");
  Serial.println("[MQTT RX] NHẬN LỆNH ĐIỀU KHIỂN TỪ BACKEND/APP");
  Serial.print("Topic:   "); Serial.println(topic);
  Serial.print("Payload: "); Serial.println(message);

  // Tự động nhận diện Device ID nếu currentDeviceId trong code đang để trống
  String topicStr = String(topic);
  int devIdx = topicStr.indexOf("/devices/");
  if (devIdx >= 0) {
    int endIdx = topicStr.indexOf("/command", devIdx + 9);
    if (endIdx > devIdx + 9) {
      String extractedId = topicStr.substring(devIdx + 9, endIdx);
      if (extractedId.length() > 0 && extractedId != "+") {
        if (currentDeviceId.length() == 0) {
          currentDeviceId = extractedId;
          Serial.println();
          Serial.println("**************************************************");
          Serial.println("[AUTO-DETECT] DA TU DONG BAT DUOC DEVICE ID: " + currentDeviceId);
          Serial.println("**************************************************");
        }
      }
    }
  }

  if (currentDeviceId.length() == 0) {
    Serial.println("[NOTICE] GATE_DEVICE_ID dang de trong \"\". ESP32 van nhan va thuc thi lenh dieu khien qua Wildcard!");
  }
  Serial.println("==================================================");

  String action = "";

  // 1. Thử parse dạng JSON: {"action":"OPEN"}
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, message);
  if (!err && doc.containsKey("action")) {
    action = String((const char*)doc["action"]);
  } else {
    // 2. Nếu là chuỗi trực tiếp: OPEN / CLOSE / STOP
    action = message;
  }

  action.toUpperCase();

  // Xử lý các lệnh MỞ CỔNG
  if (action == "OPEN" || action == "UNLOCK" || action == "POWER_OFF") {
    Serial.println("[COMMAND] -> THUC HIEN LENH: MO CONG (OPEN)");
    motorOpen();
    publishAck(action.c_str(), "SUCCESS");
  }
  // Xử lý các lệnh ĐÓNG CỔNG
  else if (action == "CLOSE" || action == "LOCK" || action == "POWER_ON") {
    Serial.println("[COMMAND] -> THUC HIEN LENH: DONG CONG (CLOSE)");
    motorClose();
    publishAck(action.c_str(), "SUCCESS");
  }
  // Xử lý lệnh DỪNG CỔNG
  else if (action == "STOP") {
    Serial.println("[COMMAND] -> THUC HIEN LENH: DUNG CONG (STOP)");
    motorStop();
    publishAck(action.c_str(), "SUCCESS");
  }
  else {
    Serial.println("[WARN] Khong ro lenh dieu khien: " + action);
    publishAck(action.c_str(), "UNKNOWN_ACTION");
  }
}

// =============================================================================
// KẾT NỐI MQTT & ĐĂNG KÝ THIẾT BỊ VỚI BACKEND (ADD DEVICE ONBOARDING)
// =============================================================================
bool connectAndPublishMQTT() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println("DANG KET NOI TOI MQTT BROKER...");
  Serial.printf("Broker: %s:%d\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);
  Serial.println("==================================================");

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  String clientId = "SYNA-GATE-" + currentNodeId + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  // Topic báo trạng thái mất kết nối khẩn cấp (Last Will and Testament)
  String willTopic = "hesta/nodes/" + currentNodeId + "/status";

  int retries = 0;
  while (!mqttClient.connected() && retries < 3) {
    if (mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, "OFFLINE")) {
      Serial.println("[MQTT] KET NOI BROKER THANH CONG!");
      break;
    } else {
      retries++;
      Serial.printf("[MQTT ERROR] Ket noi that bai, rc=%d. Thu lai trong 1 giay...\n", mqttClient.state());
      delay(1000);
    }
  }

  if (!mqttClient.connected()) {
    Serial.println("[ERROR] Khong the ket noi MQTT Broker!");
    notifyBleStatus("mqtt_failed", "error", "broker_unreachable");
    return false;
  }

  // ---------------------------------------------------------------------------
  // 1. PUBLISH DISCOVERY MESSAGE ĐẾN BACKEND ĐỂ HOÀN TẤT ADD DEVICE
  // Topic: hesta/nodes/{nodeId}/discovery
  // Backend MqttMessageReceiver.java sẽ nhận và đăng ký EdgeNode thành ONLINE
  // ---------------------------------------------------------------------------
  String discoveryTopic = "hesta/nodes/" + currentNodeId + "/discovery";

  JsonDocument discDoc;
  discDoc["nodeId"] = currentNodeId;
  discDoc["macAddress"] = WiFi.macAddress();
  discDoc["firmwareVersion"] = FIRMWARE_VERSION;
  discDoc["deviceName"] = DEFAULT_DEVICE_NAME;
  discDoc["status"] = "ONLINE";

  JsonArray caps = discDoc["capabilities"].to<JsonArray>();
  caps.add("gate");
  caps.add("motor");
  caps.add("gpio");
  caps.add("sensor");
  caps.add("relay");

  String discPayload;
  serializeJson(discDoc, discPayload);

  Serial.println("[MQTT PUB DISCOVERY] Topic: " + discoveryTopic);
  Serial.println("[MQTT PUB DISCOVERY] Payload: " + discPayload);
  mqttClient.publish(discoveryTopic.c_str(), discPayload.c_str());

  // ---------------------------------------------------------------------------
  // 2. PUBLISH ONLINE STATUS CHO NODE VÀ GATE DEVICE
  // ---------------------------------------------------------------------------
  String nodeStatusTopic = "hesta/nodes/" + currentNodeId + "/status";
  mqttClient.publish(nodeStatusTopic.c_str(), "ONLINE");

  if (currentDeviceId.length() > 0) {
    String deviceStatusTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/status";
    mqttClient.publish(deviceStatusTopic.c_str(), "ONLINE");
  }

  // ---------------------------------------------------------------------------
  // 3. SUBSCRIBE NHẬN LỆNH ĐIỀU KHIỂN CỔNG
  // ---------------------------------------------------------------------------
  Serial.println("[MQTT SUB] Dang dang ky lang nghe cac topic command:");

  // A. Lệnh theo Device ID cụ thể nếu đã được điền tay
  if (currentDeviceId.length() > 0) {
    String cmdTopic1 = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/command";
    String cmdTopic2 = "hesta/nodes/+/devices/" + currentDeviceId + "/command";
    String cmdTopicDirect = "hesta/devices/" + currentDeviceId + "/command";
    mqttClient.subscribe(cmdTopic1.c_str());
    mqttClient.subscribe(cmdTopic2.c_str());
    mqttClient.subscribe(cmdTopicDirect.c_str());
    Serial.println(" -> " + cmdTopic1);
    Serial.println(" -> " + cmdTopic2);
    Serial.println(" -> " + cmdTopicDirect);
  }

  // B. LUÔN SUBSCRIBE WILDCARD: Giúp nhận mọi lệnh điều khiển kể cả khi chưa điền Device ID!
  String cmdTopicWildcard1 = "hesta/nodes/" + currentNodeId + "/devices/+/command";
  String cmdTopicWildcardAll = "hesta/nodes/+/devices/+/command";
  String cmdTopicNode = "hesta/nodes/" + currentNodeId + "/command";
  mqttClient.subscribe(cmdTopicWildcard1.c_str());
  mqttClient.subscribe(cmdTopicWildcardAll.c_str());
  mqttClient.subscribe(cmdTopicNode.c_str());
  Serial.println(" -> " + cmdTopicWildcard1);
  Serial.println(" -> " + cmdTopicWildcardAll);
  Serial.println(" -> " + cmdTopicNode);

  // C. Hỗ trợ thêm thiết bị cũ (Legacy 2d566d0a...) để bạn test song song không bị gián đoạn
  if (String(LEGACY_GATE_DEVICE_ID).length() > 0 && currentDeviceId != LEGACY_GATE_DEVICE_ID) {
    String legacyCmd = "hesta/nodes/+/devices/" + String(LEGACY_GATE_DEVICE_ID) + "/command";
    mqttClient.subscribe(legacyCmd.c_str());
    Serial.println(" -> [LEGACY] " + legacyCmd);
  }

  // ---------------------------------------------------------------------------
  // 4. PUBLISH TRẠNG THÁI HIỆN TẠI CỦA CỔNG
  // ---------------------------------------------------------------------------
  publishState();
  publishTelemetry();

  // ---------------------------------------------------------------------------
  // 5. BÁO THÀNH CÔNG VỀ APP SYNA QUA BLE ĐỂ APP KẾT THÚC QUY TRÌNH ADD DEVICE!
  // App Flutter lắng nghe 'mqtt_connected' hoặc 'ready' để hoàn tất onboarding
  // ---------------------------------------------------------------------------
  notifyBleStatus("mqtt_connected", "nodeId", currentNodeId.c_str(), "broker", mqttBrokerIP.toString().c_str());
  delay(200);
  notifyBleStatus("ready", "nodeId", currentNodeId.c_str());

  return true;
}


// =============================================================================
// SERIAL COMMAND (ĐIỀU KHIỂN & TEST TRỰC TIẾP QUA SERIAL MONITOR)
// =============================================================================
void handleSerialCommand() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "OPEN") {
    Serial.println("[SERIAL] Lệnh MỞ CỔNG");
    motorOpen();
  } else if (cmd == "CLOSE") {
    Serial.println("[SERIAL] Lệnh ĐÓNG CỔNG");
    motorClose();
  } else if (cmd == "STOP") {
    Serial.println("[SERIAL] Lệnh DỪNG CỔNG");
    motorStop();
  } else if (cmd == "STATUS") {
    printGateStatus();
    publishState();
  } else if (cmd == "MQTT") {
    connectAndPublishMQTT();
  } else if (cmd == "RESET") {
    clearStoredConfig();
    Serial.println("[SERIAL] Da xoa cau hinh WiFi. Khoi dong lai ESP32...");
    ESP.restart();
  } else if (cmd.length() > 0) {
    Serial.println("\n[SERIAL] Lệnh có sẵn: OPEN, CLOSE, STOP, STATUS, MQTT, RESET");
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("      SYNA ESP32 - TEST ADD DEVICE (GATE MOTOR)   ");
  Serial.println("   BLE Provisioning -> mDNS -> MQTT Auto Onboard  ");
  Serial.println("==================================================");

  // 1. Cấu hình chân Motor L298N
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);

  // 2. Cấu hình chân công tắc hành trình
  pinMode(LIMIT_OPEN, INPUT_PULLUP);
  pinMode(LIMIT_CLOSE, INPUT_PULLUP);

  // 3. Cấu hình PWM cho ENA
  setupPWM();
  writeMotorPWM(0);
  gateState = GATE_STOPPED;

  // 4. In sơ đồ chân & trạng thái ban đầu
  Serial.println();
  Serial.println("[HARDWARE] Cau hinh chan:");
  Serial.printf(" - MOTOR IN1 : GPIO %d\n", MOTOR_IN1);
  Serial.printf(" - MOTOR IN2 : GPIO %d\n", MOTOR_IN2);
  Serial.printf(" - MOTOR ENA : GPIO %d (PWM)\n", MOTOR_ENA);
  Serial.printf(" - LIMIT OPEN: GPIO %d\n", LIMIT_OPEN);
  Serial.printf(" - LIMIT CLOSE: GPIO %d\n", LIMIT_CLOSE);
  printLimitStatus();

  // 5. Nạp cấu hình từ Flash NVS
  loadStoredConfig();

  // 6. Khởi động BLE để luôn sẵn sàng nhận lệnh Onboarding từ App
  startBLE();

  // 7. Nếu đã có WiFi lưu trong Flash, tự động kết nối
  if (wifiSSID.length() > 0) {
    Serial.println("\n[AUTO-CONNECT] Phat hien WiFi trong Flash. Dang tu dong ket noi...");
    if (connectWiFi()) {
      delay(500);
      discoverMQTT();
      connectAndPublishMQTT();
    } else {
      Serial.println("[WiFi] Ket noi WiFi cu that bai. Vui long mo App de cau hinh qua BLE.");
    }
  } else {
    Serial.println("\n[BLE READY] Vui long mo App SYNA de bat dau Add Device qua BLE...");
  }

  Serial.println("\n==================================================");
  Serial.println("HE THONG DA SAN SANG!");
  Serial.println("==================================================");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  // 1. Xử lý yêu cầu cấu hình Wi-Fi từ App qua BLE (Add Device Flow)
  if (shouldConnectWiFi) {
    shouldConnectWiFi = false;

    // Bước 1: Kết nối Wi-Fi
    if (connectWiFi()) {
      delay(500);

      // Bước 2: Tự động tìm kiếm MQTT Broker qua mDNS (Avahi Linux)
      discoverMQTT();

      // Bước 3: Kết nối MQTT Broker, đăng ký Discovery với Backend và báo hoàn tất về App qua BLE
      connectAndPublishMQTT();
    }
  }

  // 2. Tự động kết nối lại Wi-Fi nếu bị rớt mạng trong lúc hoạt động
  static unsigned long lastWiFiReconnectAttempt = 0;
  if (wifiSSID.length() > 0 && WiFi.status() != WL_CONNECTED && !shouldConnectWiFi) {
    unsigned long now = millis();
    if (now - lastWiFiReconnectAttempt >= 10000) {
      lastWiFiReconnectAttempt = now;
      Serial.println("\n[WiFi] Mat ket noi WiFi! Dang thu ket noi lai...");
      mqttClient.disconnect();
      mqttBrokerFound = false;
      WiFi.reconnect();
    }
  }

  // 3. Duy trì kết nối MQTT và lắng nghe lệnh điều khiển
  if (WiFi.status() == WL_CONNECTED) {
    if (mqttClient.connected()) {
      mqttClient.loop();
    } else {
      // Reconnect MQTT định kỳ
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = now;
        if (!mqttBrokerFound) {
          discoverMQTT();
        }
        if (mqttBrokerFound) {
          connectAndPublishMQTT();
        }
      }
    }
  }

  // 4. Kiểm tra công tắc hành trình để tự động ngắt motor khi cổng mở/đóng hết cỡ
  checkLimitSwitch();

  // 5. Gửi dữ liệu Telemetry định kỳ lên Backend (mỗi 30s)
  if (millis() - lastTelemetryMillis >= TELEMETRY_INTERVAL) {
    lastTelemetryMillis = millis();
    publishTelemetry();
  }

  // 6. Xử lý lệnh điều khiển qua Serial Monitor (OPEN, CLOSE, STOP, STATUS, MQTT, RESET)
  handleSerialCommand();

  delay(10);
}
