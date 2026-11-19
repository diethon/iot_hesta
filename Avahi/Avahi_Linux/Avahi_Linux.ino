#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =====================================================
// BLE UUID - Tối giản: 1 Service UUID + 1 Characteristic UUID duy nhất
// =====================================================

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789000"
// Characteristic hỗ trợ cả WRITE (nhận lệnh từ App) và NOTIFY (báo trạng thái về App)
#define COMM_CHAR_UUID      "12345678-1234-1234-1234-123456789001"

// =====================================================
// DEVICE & MQTT CONFIG
// =====================================================

#define DEFAULT_NODE_ID     "esp32-001"
#define DEFAULT_DEVICE_NAME "SYNA-ESP32S3"
#define FIRMWARE_VERSION    "1.0.0"

// Fallback MQTT broker IP nếu mDNS không tìm thấy
#define FALLBACK_MQTT_IP    "192.168.137.107"
#define FALLBACK_MQTT_PORT  1883

// =====================================================
// GLOBAL VARIABLES
// =====================================================

BLEServer *bleServer = nullptr;
BLECharacteristic *commCharacteristic = nullptr;

bool bleClientConnected = false;
bool shouldConnectWiFi = false;

String wifiSSID = "";
String wifiPassword = "";

IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = 1883;
bool mqttBrokerFound = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =====================================================
// HELPER: Gửi thông báo trạng thái về App qua BLE Notify
// =====================================================

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

  Serial.println();
  Serial.print("[BLE NOTIFY -> APP] ");
  Serial.println(jsonStr);
}

// =====================================================
// BLE Server Callback
// =====================================================

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
    Serial.println();
    Serial.println("=================================");
    Serial.println("[BLE] CLIENT CONNECTED (Phone/App)");
    Serial.println("=================================");
    notifyBleStatus("ble_connected", "device", DEFAULT_DEVICE_NAME);
  }

  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;
    Serial.println();
    Serial.println("[BLE] CLIENT DISCONNECTED -> Restart Advertising");
    BLEDevice::startAdvertising();
  }
};

// =====================================================
// BLE Characteristic Callback: Nhận 1 gói JSON duy nhất từ App
// Ví dụ:
// {
//   "action": "set_wifi",
//   "ssid": "Dung Home T3",
//   "password": "123lyvanphuc"
// }
// =====================================================

class CommCharacteristicCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String payload = characteristic->getValue().c_str();
    payload.trim();

    Serial.println();
    Serial.println("=================================");
    Serial.print("[BLE RX] Received payload: ");
    Serial.println(payload);
    Serial.println("=================================");

    if (payload.length() == 0) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[ERROR] deserializeJson() failed: ");
      Serial.println(error.c_str());
      notifyBleStatus("error", "message", "invalid_json_format");
      return;
    }

    const char *action = doc["action"] | "";

    if (strcmp(action, "set_wifi") == 0) {
      wifiSSID = (const char*)(doc["ssid"] | "");
      wifiPassword = (const char*)(doc["password"] | "");

      if (wifiSSID.length() == 0) {
        Serial.println("[ERROR] SSID is empty");
        notifyBleStatus("wifi_failed", "error", "empty_ssid");
        return;
      }

      Serial.println("[BLE] Valid WiFi config received:");
      Serial.print(" - SSID: ");
      Serial.println(wifiSSID);
      Serial.print(" - Password length: ");
      Serial.println(wifiPassword.length());

      // Phản hồi ngay cho App: Đang bắt đầu kết nối Wi-Fi
      notifyBleStatus("connecting_wifi", "ssid", wifiSSID.c_str());

      shouldConnectWiFi = true;
    } else {
      Serial.print("[WARN] Unknown action: ");
      Serial.println(action);
      notifyBleStatus("error", "message", "unknown_action");
    }
  }
};

// =====================================================
// Start BLE Service
// =====================================================

void startBLE() {
  Serial.println("Initializing BLE Device [SYNA-ESP32S3]...");

  BLEDevice::init(DEFAULT_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new MyServerCallbacks());

  BLEService *service = bleServer->createService(SERVICE_UUID);

  // Khởi tạo 1 Characteristic duy nhất cho cả Đọc, Ghi và Thông báo
  commCharacteristic = service->createCharacteristic(
    COMM_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  commCharacteristic->setCallbacks(new CommCharacteristicCallback());
  commCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();

  // Đặt Tên thiết bị vào gói quảng bá chính (Primary Advertisement Packet)
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

  Serial.println("=============================================");
  Serial.println("BLE Ready & Advertising as: SYNA-ESP32S3");
  Serial.print("Service UUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("Comm UUID:    ");
  Serial.println(COMM_CHAR_UUID);
  Serial.println("=============================================");
}

// =====================================================
// Connect WiFi
// =====================================================

bool connectWiFi() {
  Serial.println();
  Serial.println("=================================");
  Serial.println("CONNECTING TO WIFI...");
  Serial.print("SSID: ");
  Serial.println(wifiSSID);
  Serial.println("=================================");

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
      Serial.println();
      Serial.println("[ERROR] WiFi auth failed (Sai mat khau WiFi)!");
      notifyBleStatus("wifi_failed", "error", "sai_mat_khau_wifi");
      return false;
    }
    if (st == WL_NO_SSID_AVAIL) {
      Serial.println();
      Serial.println("[ERROR] WiFi SSID not found (Khong tim thay SSID)!");
      notifyBleStatus("wifi_failed", "error", "khong_tim_thay_ssid");
      return false;
    }

    // Timeout 15s
    if (millis() - startTime > 15000) {
      Serial.println();
      Serial.println("[ERROR] WiFi connection timeout (Sai mat khau hoac song yeu)!");
      notifyBleStatus("wifi_failed", "error", "timeout_sai_mat_khau");
      return false;
    }
  }

  Serial.println();
  Serial.println("=================================");
  Serial.println("WiFi CONNECTED SUCCESSFULLY!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway:    ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("RSSI:       ");
  Serial.println(WiFi.RSSI());
  Serial.println("=================================");

  // Báo thành công qua BLE về App cùng IP nhận được
  notifyBleStatus("wifi_connected", "ip", WiFi.localIP().toString().c_str());
  return true;
}

// =====================================================
// Discover MQTT Broker qua mDNS
// =====================================================

bool discoverMQTT() {
  Serial.println();
  Serial.println("=================================");
  Serial.println("SEARCHING MQTT BROKER VIA mDNS...");
  Serial.println("Service: _syna-mqtt._tcp");
  Serial.println("=================================");

  if (!MDNS.begin("syna-esp32")) {
    Serial.println("[ERROR] Failed to start mDNS responder");
  }

  int count = MDNS.queryService("syna-mqtt", "tcp");

  if (count <= 0) {
    Serial.println("[WARN] No mDNS _syna-mqtt._tcp service found.");
    Serial.println("Using fallback MQTT broker: " FALLBACK_MQTT_IP);

    mqttBrokerIP.fromString(FALLBACK_MQTT_IP);
    mqttBrokerPort = FALLBACK_MQTT_PORT;
    mqttBrokerFound = false;
    return false;
  }

  Serial.print("Found ");
  Serial.print(count);
  Serial.println(" MQTT service(s)!");

  mqttBrokerIP = MDNS.address(0);
  mqttBrokerPort = MDNS.port(0);
  mqttBrokerFound = true;

  Serial.println("---------------------------------");
  Serial.print("Discovered Broker IP:   ");
  Serial.println(mqttBrokerIP);
  Serial.print("Discovered Broker Port: ");
  Serial.println(mqttBrokerPort);
  Serial.println("---------------------------------");

  notifyBleStatus("mdns_discovered", "broker", mqttBrokerIP.toString().c_str(), "port", String(mqttBrokerPort).c_str());
  return true;
}

// =====================================================
// Connect & Publish to Backend MQTT Broker
// =====================================================

bool connectAndPublishMQTT() {
  Serial.println();
  Serial.println("=================================");
  Serial.println("CONNECTING TO MQTT BROKER...");
  Serial.println("=================================");

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);

  String clientId = String(DEFAULT_NODE_ID) + "-" + String(random(1000, 9999));
  Serial.print("MQTT Client ID: ");
  Serial.println(clientId);

  int retries = 0;
  while (!mqttClient.connected() && retries < 3) {
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT Broker connected successfully!");
      break;
    } else {
      retries++;
      Serial.print("MQTT connect failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(". Retrying in 1s...");
      delay(1000);
    }
  }

  if (!mqttClient.connected()) {
    Serial.println("[ERROR] Could not connect to MQTT broker!");
    notifyBleStatus("mqtt_failed", "error", "broker_unreachable");
    return false;
  }

  // ---------------------------------------------------
  // 1. Publish Discovery Message tới Backend
  // Topic: hesta/nodes/{nodeId}/discovery
  // ---------------------------------------------------
  String discoveryTopic = "hesta/nodes/" + String(DEFAULT_NODE_ID) + "/discovery";

  JsonDocument discDoc;
  discDoc["nodeId"] = DEFAULT_NODE_ID;
  discDoc["macAddress"] = WiFi.macAddress();
  discDoc["firmwareVersion"] = FIRMWARE_VERSION;
  discDoc["deviceName"] = DEFAULT_DEVICE_NAME;
  discDoc["status"] = "ONLINE";

  JsonArray caps = discDoc["capabilities"].to<JsonArray>();
  caps.add("gpio");
  caps.add("sensor");
  caps.add("relay");

  String discPayload;
  serializeJson(discDoc, discPayload);

  Serial.print("[MQTT PUB] Topic: ");
  Serial.println(discoveryTopic);
  Serial.print("[MQTT PUB] Payload: ");
  Serial.println(discPayload);

  mqttClient.publish(discoveryTopic.c_str(), discPayload.c_str());

  // ---------------------------------------------------
  // 2. Publish Online Status
  // Topic: hesta/nodes/{nodeId}/status
  // ---------------------------------------------------
  String statusTopic = "hesta/nodes/" + String(DEFAULT_NODE_ID) + "/status";
  mqttClient.publish(statusTopic.c_str(), "ONLINE");
  Serial.print("[MQTT PUB] Status: ONLINE to ");
  Serial.println(statusTopic);

  // ---------------------------------------------------
  // 3. Báo thành công hoàn tất về App qua BLE!
  // ---------------------------------------------------
  notifyBleStatus("mqtt_connected", "nodeId", DEFAULT_NODE_ID, "broker", mqttBrokerIP.toString().c_str());
  delay(200);
  notifyBleStatus("ready", "nodeId", DEFAULT_NODE_ID);

  return true;
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=============================================");
  Serial.println("        SYNA ESP32-S3 ONBOARDING NODE");
  Serial.println("    Single UUID BLE -> WiFi -> mDNS -> MQTT");
  Serial.println("=============================================");

  startBLE();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  // Duy trì kết nối MQTT nếu đã kết nối
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Xử lý sự kiện nhận cấu hình Wi-Fi từ App qua BLE
  if (shouldConnectWiFi) {
    shouldConnectWiFi = false;

    // Bước 1: Kết nối Wi-Fi
    if (connectWiFi()) {
      delay(500);

      // Bước 2: Tìm MQTT broker qua mDNS
      discoverMQTT();

      // Bước 3: Kết nối MQTT broker và publish đăng ký với Backend
      connectAndPublishMQTT();
    }
  }

  delay(50);
}