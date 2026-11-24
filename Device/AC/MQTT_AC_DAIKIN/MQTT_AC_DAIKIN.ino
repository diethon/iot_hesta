#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>

// ============================================================
// HESTA / SYNA SMART HOME - DAIKIN 64-BIT AC CONTROLLER
// 
// Tính năng:
// 1. Dựa trên giao thức DAIKIN64 (64-bit) từ remote điều hòa Daikin.
// 2. BLE WiFi Provisioning theo chuẩn Hesta (Service UUID ...9000, Char ...9001).
// 3. Tự động lưu cấu hình WiFi & MQTT vào bộ nhớ Flash NVS (Preferences).
// 4. mDNS Auto-Discovery: tự động tìm kiếm Mosquitto Broker (_syna-mqtt._tcp).
// 5. Kết nối MQTT với Device ID: 05d8a4fd-d7f1-4c62-a70e-a4213552b054.
// 6. Xử lý các lệnh từ Backend Hesta:
//    - SET_POWER (ON / OFF / TOGGLE) có cơ chế quản lý bit Power Toggle của DAIKIN64.
//    - SET_TEMPERATURE (16°C - 30°C), TEMPERATURE_PLUS, TEMPERATURE_MINUS.
//    - SET_MODE (COOL, HEAT, DRY, FAN).
//    - SET_FAN (AUTO, LOW, MID, HIGH, TURBO, QUIET).
//    - SET_SWING (ON / OFF - đảo gió đứng).
//    - GET_STATE (Yêu cầu gửi trạng thái hiện tại).
//    - RAW (Phát lại gói xung thô 137 xung mẫu để kiểm tra phần cứng IR).
// 7. Gửi State, ACK (xác nhận lệnh kèm commandId), Status (LWT ONLINE/OFFLINE)
//    và Telemetry định kỳ lên Broker.
// 8. Giữ lại giao tiếp điều khiển qua cổng Serial Monitor (115200 baud).
// ============================================================

// ============================================================
// HARDWARE PIN CONFIGURATION
// ============================================================
#define IR_LED_PIN 4

// ============================================================
// BACKEND HESTA - NODE & DEVICE IDENTIFIERS
// ============================================================
#define DEFAULT_NODE_ID  "32442d7f-dfcf-4b2f-a380-adba32876cec"
#define DEVICE_ID        "05d8a4fd-d7f1-4c62-a70e-a4213552b054"
#define DEVICE_TYPE      "AIR_CONDITIONER"

// ============================================================
// BLE PROVISIONING CONFIGURATION (SYNA MOBILE APP)
// ============================================================
#define BLE_DEVICE_NAME        "SYNA-DAIKIN-AC"
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID     "12345678-1234-1234-1234-123456789001"

// ============================================================
// MQTT & mDNS CONFIGURATION
// ============================================================
#define MQTT_DISCOVERY_SERVICE  "_syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "_tcp"
#define DEFAULT_MQTT_PORT       1883
#define FALLBACK_MQTT_IP        "192.168.1.100" // IP dự phòng nếu mDNS không tìm thấy

// ============================================================
// TOPIC DEFINITIONS
// ============================================================
String currentNodeId = DEFAULT_NODE_ID;

const String MQTT_SUB_WILDCARD_COMMAND = "hesta/nodes/+/devices/" + String(DEVICE_ID) + "/command";
const String MQTT_SUB_DIRECT_COMMAND   = "hesta/devices/" + String(DEVICE_ID) + "/command";

// ============================================================
// GLOBAL OBJECTS & DRIVERS
// ============================================================
IRDaikin64 ac(IR_LED_PIN);
IRsend irsend(IR_LED_PIN);

Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

BLEServer* bleServer = nullptr;
BLECharacteristic* commCharacteristic = nullptr;

bool bleClientConnected = false;
volatile bool shouldConnectWiFi = false;

String wifiSSID = "";
String wifiPassword = "";

bool mqttBrokerFound = false;
IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = DEFAULT_MQTT_PORT;

// Virtual State của điều hòa
bool acPower = false;
uint8_t currentTemp = 26;
uint8_t currentMode = kDaikin64Cool;
uint8_t currentFan  = kDaikin64FanAuto;
bool currentSwing   = false;

// Timers
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastDiscoveryAttempt = 0;
unsigned long lastTelemetryPublish = 0;

// Gói raw đã bắt được từ remote gốc Daikin (Cool, 26C, Fan Low, kèm Power Toggle)
const uint16_t rawData[137] = {
  9774, 9814,  9796, 9832,  4568, 2526,  336, 374,  356, 952,
  356,  952,   356,  374,   356,  952,   356, 374,  356, 374,
  356,  374,   356,  374,   356,  952,   356, 374,  356, 374,
  356,  374,   356,  374,   356,  372,   356, 952,  356, 374,
  356,  952,   356,  374,   356,  374,   354, 374,  356, 374,
  356,  372,   356,  374,   356,  374,   356, 952,  354, 954,
  354,  374,   356,  374,   356,  374,   356, 372,  356, 374,
  358,  374,   356,  374,   356,  374,   356, 374,  356, 952,
  356,  374,   356,  374,   356,  372,   358, 374,  356, 374,
  358,  372,   356,  374,   356,  954,   354, 374,  356, 372,
  356,  374,   356,  374,   356,  952,   356, 954,  356, 374,
  356,  374,   356,  952,   356,  374,   356, 374,  356, 952,
  356,  374,   356,  952,   356,  952,   356, 372,  356, 374,
  356,  374,   356,  374,   356, 20324, 4568
};

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void loadStoredWiFi();
void saveStoredWiFi(const String& ssid, const String& password);
void clearStoredWiFi();
void loadStoredMQTT();
void saveStoredMQTT(const String& host, uint16_t port);
void loadStoredACState();
void saveStoredACState();

void startBLE();
void notifyBLE(const String& message);

void handleWiFiProvisioning();
bool discoverMQTT();
bool connectMQTT();

void sendIRState(bool toggle);
void sendRawSignal();

void publishACState();
void publishACK(const char* commandId);
void publishStatus(const char* status);
void publishTelemetry();

void processCommand(const char* action, JsonObject& params, const char* commandId = nullptr);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleSerial();
void printStatus();

const char* modeToString(uint8_t mode);
uint8_t stringToMode(String str);
const char* fanToString(uint8_t fan);
uint8_t stringToFan(String str);

// ============================================================
// HELPER CONVERSIONS
// ============================================================
const char* modeToString(uint8_t mode) {
  switch (mode) {
    case kDaikin64Cool: return "COOL";
    case kDaikin64Heat: return "HEAT";
    case kDaikin64Dry:  return "DRY";
    case kDaikin64Fan:  return "FAN";
    default:            return "COOL";
  }
}

uint8_t stringToMode(String str) {
  str.toUpperCase();
  if (str == "HEAT") return kDaikin64Heat;
  if (str == "DRY")  return kDaikin64Dry;
  if (str == "FAN")  return kDaikin64Fan;
  return kDaikin64Cool;
}

const char* fanToString(uint8_t fan) {
  switch (fan) {
    case kDaikin64FanAuto:  return "AUTO";
    case kDaikin64FanLow:   return "LOW";
    case kDaikin64FanMed:   return "MID";
    case kDaikin64FanHigh:  return "HIGH";
    case kDaikin64FanTurbo: return "TURBO";
    case kDaikin64FanQuiet: return "QUIET";
    default:                return "AUTO";
  }
}

uint8_t stringToFan(String str) {
  str.toUpperCase();
  if (str == "LOW" || str == "1" || str == "MIN" || str == "F1")    return kDaikin64FanLow;
  if (str == "MID" || str == "MED" || str == "2" || str == "F2")    return kDaikin64FanMed;
  if (str == "HIGH" || str == "3" || str == "MAX" || str == "F3")   return kDaikin64FanHigh;
  if (str == "TURBO" || str == "FTURBO")                           return kDaikin64FanTurbo;
  if (str == "QUIET" || str == "FQUIET")                           return kDaikin64FanQuiet;
  return kDaikin64FanAuto;
}

// ============================================================
// DAIKIN 64 IR CONTROL LOGIC
// ============================================================
void sendIRState(bool toggle) {
  ac.setMode(currentMode);
  ac.setTemp(currentTemp);
  ac.setFan(currentFan);
  ac.setSwingVertical(currentSwing);

  // DAIKIN64 dùng bit Power Toggle:
  // Chỉ khi toggle == true thì mới gửi bit đảo nguồn.
  ac.setPowerToggle(toggle);
  ac.send();
  ac.setPowerToggle(false); // Đặt lại false ngay lập tức

  Serial.println("[IR DAIKIN64] Đã phát sóng IR:");
  Serial.print("              Nguồn        : "); Serial.println(acPower ? "BẬT (ON)" : "TẮT (OFF)");
  Serial.print("              Toggle Bit   : "); Serial.println(toggle ? "1 (ĐẢO NGUỒN)" : "0 (GIỮ NGUYÊN)");
  Serial.print("              Nhiệt độ     : "); Serial.print(currentTemp); Serial.println(" °C");
  Serial.print("              Chế độ       : "); Serial.println(modeToString(currentMode));
  Serial.print("              Tốc độ quạt  : "); Serial.println(fanToString(currentFan));
  Serial.print("              Đảo gió dọc  : "); Serial.println(currentSwing ? "BẬT" : "TẮT");
  Serial.println("              Chi tiết gói : " + ac.toString());

  saveStoredACState();
}

void sendRawSignal() {
  irsend.sendRaw(rawData, 137, 38);
  acPower = !acPower;
  Serial.println("[IR RAW] Đã phát gói xung raw 137 xung 38kHz (Đã đảo trạng thái nguồn).");
  saveStoredACState();
  publishACState();
}

// ============================================================
// BLE CALLBACKS
// ============================================================
class SynaBLEServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleClientConnected = true;
    Serial.println("[BLE] Mobile App đã kết nối!");
  }

  void onDisconnect(BLEServer* server) override {
    bleClientConnected = false;
    Serial.println("[BLE] Mobile App đã ngắt kết nối. Đang phát lại quảng bá...");
    BLEDevice::startAdvertising();
  }
};

class SynaBLECharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue().c_str();
    value.trim();
    if (value.isEmpty()) return;

    Serial.print("[BLE NHẬN]: ");
    Serial.println(value);

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      notifyBLE("{\"status\":\"error\",\"message\":\"invalid_json\"}");
      Serial.println("[BLE LỖI]: JSON không hợp lệ");
      return;
    }

    const char* action = doc["action"] | "";

    // 1. Cấu hình WiFi
    if (String(action) == "set_wifi") {
      const char* ssid = doc["ssid"] | "";
      const char* password = doc["password"] | "";

      if (strlen(ssid) == 0) {
        notifyBLE("{\"status\":\"error\",\"message\":\"ssid_required\"}");
        return;
      }

      wifiSSID = String(ssid);
      wifiPassword = String(password);

      if (doc.containsKey("mqtt_host")) {
        const char* mHost = doc["mqtt_host"] | "";
        int mPort = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
        if (strlen(mHost) > 0) {
          if (mqttBrokerIP.fromString(mHost)) {
            mqttBrokerPort = mPort;
            mqttBrokerFound = true;
            saveStoredMQTT(mHost, mPort);
            Serial.printf("[BLE] Nhận cấu hình MQTT: %s:%d\n", mHost, mPort);
          }
        }
      }

      shouldConnectWiFi = true;
      notifyBLE("{\"status\":\"connecting_wifi\",\"message\":\"wifi_credentials_received\"}");
      Serial.println("[BLE] Đã nhận WiFi credentials, chuẩn bị kết nối...");
    }
    // 2. Xóa WiFi
    else if (String(action) == "reset_wifi") {
      clearStoredWiFi();
      wifiSSID = "";
      wifiPassword = "";
      mqttBrokerFound = false;
      WiFi.disconnect(true);
      mqttClient.disconnect();
      notifyBLE("{\"status\":\"wifi_reset\",\"message\":\"WiFi credentials cleared\"}");
      Serial.println("[BLE] Đã xóa thông tin WiFi trong NVS");
    }
    // 3. Yêu cầu lấy thông tin thiết bị
    else if (String(action) == "get_info") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument info;
#else
      DynamicJsonDocument info(512);
#endif
      info["deviceId"] = DEVICE_ID;
      info["deviceType"] = DEVICE_TYPE;
      info["power"] = acPower;
      info["temperature"] = currentTemp;
      info["mode"] = modeToString(currentMode);
      info["fan"] = fanToString(currentFan);
      info["swing"] = currentSwing;
      info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      info["ip"] = WiFi.localIP().toString();
      info["mqtt_connected"] = mqttClient.connected();

      char buf[384];
      serializeJson(info, buf);
      notifyBLE(String(buf));
    }
    // 4. Lệnh điều khiển điều hòa trực tiếp qua BLE
    else if (String(action).startsWith("SET_") || String(action) == "TOGGLE") {
      JsonObject obj = doc.as<JsonObject>();
      processCommand(action, obj, nullptr);
      notifyBLE("{\"status\":\"success\",\"message\":\"command_executed\"}");
    } else {
      notifyBLE("{\"status\":\"error\",\"message\":\"unknown_action\"}");
    }
  }
};

// ============================================================
// BLE INITIALIZATION
// ============================================================
void startBLE() {
  BLEDevice::init(BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new SynaBLEServerCallbacks());

  BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  commCharacteristic = service->createCharacteristic(
    BLE_COMM_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  commCharacteristic->addDescriptor(new BLE2902());
  commCharacteristic->setCallbacks(new SynaBLECharacteristicCallbacks());
  commCharacteristic->setValue("{\"status\":\"ready\",\"device\":\"" BLE_DEVICE_NAME "\"}");
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("[BLE] Đang phát quảng bá với tên: %s\n", BLE_DEVICE_NAME);
}

void notifyBLE(const String& message) {
  if (commCharacteristic != nullptr && bleClientConnected) {
    commCharacteristic->setValue(message.c_str());
    commCharacteristic->notify();
    Serial.printf("[BLE GỬI APP]: %s\n", message.c_str());
  }
}

// ============================================================
// NVS STORAGE (PREFERENCES)
// ============================================================
void loadStoredWiFi() {
  preferences.begin("wifi", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  if (wifiSSID.length() > 0) {
    Serial.printf("[NVS] Tìm thấy WiFi đã lưu: %s\n", wifiSSID.c_str());
  } else {
    Serial.println("[NVS] Chưa có WiFi trong bộ nhớ flash.");
  }
}

void saveStoredWiFi(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  Serial.println("[NVS] Đã lưu thông tin WiFi vào flash.");
}

void loadStoredMQTT() {
  preferences.begin("mqtt", true);
  String storedHost = preferences.getString("host", "");
  uint16_t storedPort = preferences.getUShort("port", 0);
  preferences.end();

  if (storedHost.length() > 0 && storedPort > 0) {
    if (mqttBrokerIP.fromString(storedHost)) {
      mqttBrokerPort = storedPort;
      mqttBrokerFound = true;
      Serial.printf("[NVS] Đã nạp MQTT Broker đã lưu: %s:%d\n", storedHost.c_str(), storedPort);
    }
  }
}

void saveStoredMQTT(const String& host, uint16_t port) {
  preferences.begin("mqtt", false);
  preferences.putString("host", host);
  preferences.putUShort("port", port);
  preferences.end();
  Serial.println("[NVS] Đã lưu thông tin MQTT Broker vào flash.");
}

void clearStoredWiFi() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  preferences.begin("mqtt", false);
  preferences.clear();
  preferences.end();

  mqttBrokerFound = false;
  Serial.println("[NVS] Đã xóa toàn bộ cấu hình WiFi & MQTT trong flash.");
}

void loadStoredACState() {
  preferences.begin("ac_state", true);
  acPower      = preferences.getBool("power", false);
  currentTemp  = preferences.getUChar("temp", 26);
  currentMode  = preferences.getUChar("mode", kDaikin64Cool);
  currentFan   = preferences.getUChar("fan", kDaikin64FanAuto);
  currentSwing = preferences.getBool("swing", false);
  preferences.end();

  if (currentTemp < 16 || currentTemp > 30) currentTemp = 26;
  Serial.println("[NVS] Đã khôi phục trạng thái điều hòa từ flash.");
}

void saveStoredACState() {
  preferences.begin("ac_state", false);
  preferences.putBool("power", acPower);
  preferences.putUChar("temp", currentTemp);
  preferences.putUChar("mode", currentMode);
  preferences.putUChar("fan", currentFan);
  preferences.putBool("swing", currentSwing);
  preferences.end();
}

// ============================================================
// WIFI CONNECTION & PROVISIONING
// ============================================================
void handleWiFiProvisioning() {
  shouldConnectWiFi = false;
  notifyBLE("{\"status\":\"connecting_wifi\"}");

  mqttClient.disconnect();
  WiFi.disconnect(true);
  delay(200);

  Serial.println();
  Serial.println("======================================");
  Serial.printf("[WiFi] Đang kết nối tới: %s\n", wifiSSID.c_str());
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] KẾT NỐI THÀNH CÔNG!");
    Serial.printf("[WiFi] IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

    saveStoredWiFi(wifiSSID, wifiPassword);

    String okJson = "{\"status\":\"wifi_connected\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    notifyBLE(okJson);

    delay(1000);
    mqttBrokerFound = false;
    if (discoverMQTT()) {
      if (connectMQTT()) {
        notifyBLE("{\"status\":\"mqtt_connected\"}");
      }
    }
  } else {
    Serial.println("[WiFi] Kết nối thất bại. Vui lòng kiểm tra lại mật khẩu.");
    notifyBLE("{\"status\":\"wifi_failed\",\"error\":\"Sai mật khẩu hoặc sóng yếu\"}");
    WiFi.disconnect(true);
  }
}

// ============================================================
// mDNS AUTO-DISCOVERY (MQTT BROKER)
// ============================================================
bool discoverMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.println();
  Serial.println("======================================");
  Serial.println("[mDNS] Đang tìm kiếm MQTT broker qua DNS-SD...");
  Serial.println("       Dịch vụ: _syna-mqtt._tcp.local");
  Serial.println("======================================");

  MDNS.end();
  String mdnsHost = "syna-ac-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!MDNS.begin(mdnsHost.c_str())) {
    Serial.println("[mDNS] Không thể khởi tạo mDNS responder");
    return false;
  }

  int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);
  if (count <= 0) {
    count = MDNS.queryService("syna-mqtt", "tcp");
  }

  Serial.printf("[mDNS] Số broker tìm thấy: %d\n", count);

  if (count > 0) {
    for (int i = 0; i < count; i++) {
      IPAddress ip = MDNS.address(i);
      uint16_t port = MDNS.port(i);

      Serial.printf("[mDNS] Broker #%d: %s | IP: %s | Port: %d\n",
                    i + 1, MDNS.hostname(i).c_str(), ip.toString().c_str(), port);

      if (ip != IPAddress(0, 0, 0, 0) && port > 0) {
        mqttBrokerIP = ip;
        mqttBrokerPort = port;
        mqttBrokerFound = true;
        saveStoredMQTT(mqttBrokerIP.toString(), mqttBrokerPort);
        Serial.println("[mDNS] >>> TÌM THẤY MQTT BROKER THỰC TẾ THÀNH CÔNG! <<<");
        return true;
      }
    }
  }

  // Nếu không tìm thấy qua mDNS nhưng trong NVS hoặc Fallback có sẵn
  if (!mqttBrokerFound && strlen(FALLBACK_MQTT_IP) > 0) {
    if (mqttBrokerIP.fromString(FALLBACK_MQTT_IP)) {
      mqttBrokerPort = DEFAULT_MQTT_PORT;
      Serial.printf("[mDNS] Sử dụng IP fallback: %s:%d\n", FALLBACK_MQTT_IP, mqttBrokerPort);
      return true;
    }
  }

  Serial.println("[mDNS] Chưa tìm thấy Broker, sẽ tự động thử lại sau...");
  return false;
}

// ============================================================
// CONNECT MQTT
// ============================================================
bool connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!mqttBrokerFound) {
    if (!discoverMQTT()) return false;
  }

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  String clientId = "SYNA-AC-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  String willTopic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/status";
  String willMsg   = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"status\":\"OFFLINE\"}";

  Serial.println();
  Serial.println("======================================");
  Serial.printf("[MQTT] Đang kết nối tới %s:%d (Client ID: %s)\n",
                mqttBrokerIP.toString().c_str(), mqttBrokerPort, clientId.c_str());
  Serial.println("======================================");

  if (mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, willMsg.c_str())) {
    Serial.println("[MQTT] KẾT NỐI MQTT THÀNH CÔNG!");

    mqttClient.subscribe(MQTT_SUB_WILDCARD_COMMAND.c_str());
    mqttClient.subscribe(MQTT_SUB_DIRECT_COMMAND.c_str());
    String nodeCmdTopic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/command";
    mqttClient.subscribe(nodeCmdTopic.c_str());

    Serial.printf("[MQTT] Subscribed topic: %s\n", MQTT_SUB_WILDCARD_COMMAND.c_str());
    Serial.printf("[MQTT] Subscribed topic: %s\n", MQTT_SUB_DIRECT_COMMAND.c_str());
    Serial.printf("[MQTT] Subscribed topic: %s\n", nodeCmdTopic.c_str());

    publishStatus("ONLINE");
    publishACState();

    notifyBLE("{\"status\":\"mqtt_connected\"}");
    return true;
  } else {
    Serial.printf("[MQTT] Kết nối thất bại, rc=%d\n", mqttClient.state());
    mqttBrokerFound = false;
    return false;
  }
}

// ============================================================
// MQTT PUBLISHERS (STATUS, STATE, ACK, TELEMETRY)
// ============================================================
void publishStatus(const char* status) {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/status";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = DEVICE_ID;
  doc["status"] = status;
  doc["deviceType"] = DEVICE_TYPE;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATUS]: %s\n", buf);
}

void publishACState() {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/state";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = DEVICE_ID;
  doc["power"] = acPower;
  doc["temperature"] = currentTemp;
  doc["mode"] = modeToString(currentMode);
  doc["fan"] = fanToString(currentFan);
  doc["swing"] = currentSwing;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATE]: %s\n", buf);
}

void publishACK(const char* commandId) {
  if (!mqttClient.connected() || commandId == nullptr || strlen(commandId) == 0) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/ack";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(384);
#endif
  doc["commandId"] = commandId;
  doc["deviceId"] = DEVICE_ID;
  doc["status"] = "SUCCESS";

  JsonObject st = doc["state"].to<JsonObject>();
  st["power"] = acPower;
  st["temperature"] = currentTemp;
  st["mode"] = modeToString(currentMode);
  st["fan"] = fanToString(currentFan);
  st["swing"] = currentSwing;

  char buf[384];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf);
  Serial.printf("[MQTT ACK]: %s\n", buf);
}

void publishTelemetry() {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(DEVICE_ID) + "/telemetry";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(384);
#endif
  doc["deviceId"] = DEVICE_ID;
  doc["deviceType"] = DEVICE_TYPE;
  doc["power"] = acPower;
  doc["temperature"] = currentTemp;
  doc["mode"] = modeToString(currentMode);
  doc["fan"] = fanToString(currentFan);
  doc["swing"] = currentSwing;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  char buf[384];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf);
  Serial.printf("[MQTT TELEMETRY]: %s\n", buf);
}

// ============================================================
// COMMAND PROCESSOR (XỬ LÝ LỆNH TỪ BACKEND / APP)
// ============================================================
void processCommand(const char* action, JsonObject& doc, const char* commandId) {
  if (!action) return;

  String act = String(action);
  act.toUpperCase();

  bool needSendIR = false;
  bool togglePowerBit = false;

  // 1. SET_POWER / BẬT / TẮT
  if (act == "SET_POWER" || act == "POWER_ON" || act == "TURN_ON" ||
      act == "POWER_OFF" || act == "TURN_OFF" || act == "TOGGLE" || act == "POWER") {

    bool targetPower = true;
    bool isExplicitToggle = (act == "TOGGLE" || act == "POWER");

    if (act == "POWER_OFF" || act == "TURN_OFF") {
      targetPower = false;
    } else if (act == "POWER_ON" || act == "TURN_ON") {
      targetPower = true;
    } else if (doc.containsKey("power")) {
      if (doc["power"].is<bool>()) {
        targetPower = doc["power"].as<bool>();
      } else if (doc["power"].is<const char*>()) {
        String pStr = doc["power"].as<const char*>();
        pStr.toUpperCase();
        targetPower = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
      }
    }

    if (isExplicitToggle) {
      acPower = !acPower;
      togglePowerBit = true;
      needSendIR = true;
    } else {
      if (targetPower != acPower) {
        acPower = targetPower;
        togglePowerBit = true;
        needSendIR = true;
      } else {
        needSendIR = true;
        togglePowerBit = false;
      }
    }
  }
  // 2. CÀI ĐẶT NHIỆT ĐỘ
  else if (act == "SET_TEMPERATURE" || act == "SET_TEMP") {
    int t = currentTemp;
    if (doc.containsKey("temperature")) {
      t = doc["temperature"].as<int>();
    } else if (doc.containsKey("temp")) {
      t = doc["temp"].as<int>();
    }

    if (t >= 16 && t <= 30) {
      currentTemp = t;
      needSendIR = true;
      togglePowerBit = false;
    } else {
      Serial.printf("[LỖI]: Nhiệt độ %d°C ngoài dải cho phép (16-30°C)\n", t);
    }
  }
  // 3. TĂNG NHIỆT ĐỘ
  else if (act == "TEMPERATURE_PLUS" || act == "TEMP_UP") {
    if (currentTemp < 30) {
      currentTemp++;
      needSendIR = true;
      togglePowerBit = false;
    }
  }
  // 4. GIẢM NHIỆT ĐỘ
  else if (act == "TEMPERATURE_MINUS" || act == "TEMP_DOWN") {
    if (currentTemp > 16) {
      currentTemp--;
      needSendIR = true;
      togglePowerBit = false;
    }
  }
  // 5. CÀI ĐẶT TỐC ĐỘ QUẠT
  else if (act == "SET_FAN") {
    String fVal = "";
    if (doc.containsKey("fan")) fVal = doc["fan"].as<String>();
    else if (doc.containsKey("speed")) fVal = doc["speed"].as<String>();

    if (fVal.length() > 0) {
      currentFan = stringToFan(fVal);
      needSendIR = true;
      togglePowerBit = false;
    }
  }
  // 6. CÀI ĐẶT CHẾ ĐỘ
  else if (act == "SET_MODE") {
    String mVal = "";
    if (doc.containsKey("mode")) mVal = doc["mode"].as<String>();

    if (mVal.length() > 0) {
      currentMode = stringToMode(mVal);
      needSendIR = true;
      togglePowerBit = false;
    }
  }
  // 7. CÀI ĐẶT ĐẢO GIÓ (SWING)
  else if (act == "SET_SWING") {
    if (doc.containsKey("swing")) {
      if (doc["swing"].is<bool>()) {
        currentSwing = doc["swing"].as<bool>();
      } else {
        String sStr = doc["swing"].as<String>();
        sStr.toUpperCase();
        currentSwing = (sStr == "ON" || sStr == "TRUE" || sStr == "1");
      }
      needSendIR = true;
      togglePowerBit = false;
    }
  }
  // 8. LỆNH RAW
  else if (act == "RAW") {
    sendRawSignal();
    publishACK(commandId);
    return;
  }
  // 9. LỆNH LẤY TRẠNG THÁI (GET_STATE)
  else if (act == "GET_STATE") {
    publishACState();
    publishACK(commandId);
    return;
  }
  else {
    Serial.printf("[MQTT]: Lệnh không xác định: %s\n", act.c_str());
  }

  if (needSendIR) {
    sendIRState(togglePowerBit);
  }

  publishACState();
  publishACK(commandId);
}

// ============================================================
// MQTT CALLBACK (XỬ LÝ TIN NHẮN TỪ TOPIC)
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.println();
  Serial.println("======================================");
  Serial.printf("[MQTT NHẬN LỆNH] Topic: %s\n", topic);
  Serial.printf("[MQTT NHẬN LỆNH] Payload: %s\n", message);
  Serial.println("======================================");

  String topicStr = String(topic);
  int nodeIdx = topicStr.indexOf("hesta/nodes/");
  if (nodeIdx >= 0) {
    int start = nodeIdx + 12;
    int end = topicStr.indexOf("/devices/", start);
    if (end > start) {
      String extractedNode = topicStr.substring(start, end);
      if (extractedNode != "+" && extractedNode.length() > 0) {
        currentNodeId = extractedNode;
      }
    }
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(1024);
#endif

  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    String raw = String(message);
    raw.trim();
    raw.toLowerCase();
    if (raw == "on" || raw == "1") {
      if (!acPower) { acPower = true; sendIRState(true); }
      publishACState();
    } else if (raw == "off" || raw == "0") {
      if (acPower) { acPower = false; sendIRState(true); }
      publishACState();
    } else if (raw == "power") {
      acPower = !acPower;
      sendIRState(true);
      publishACState();
    } else if (raw == "raw") {
      sendRawSignal();
    }
    return;
  }

  JsonObject obj = doc.as<JsonObject>();
  const char* commandId = doc["commandId"] | "";
  const char* action    = doc["action"] | "";

  if (strlen(action) == 0) {
    if (doc.containsKey("power")) action = "SET_POWER";
    else if (doc.containsKey("temperature") || doc.containsKey("temp")) action = "SET_TEMPERATURE";
    else if (doc.containsKey("fan")) action = "SET_FAN";
    else if (doc.containsKey("mode")) action = "SET_MODE";
    else if (doc.containsKey("swing")) action = "SET_SWING";
  }

  processCommand(action, obj, commandId);
}

// ============================================================
// SERIAL INTERFACE (ĐIỀU KHIỂN & DEBUG TRỰC TIẾP)
// ============================================================
void handleSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  String lowerCmd = cmd;
  lowerCmd.toLowerCase();

  if (lowerCmd == "power") {
    acPower = !acPower;
    sendIRState(true);
    publishACState();
  }
  else if (lowerCmd == "on") {
    if (!acPower) { acPower = true; sendIRState(true); }
    else sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "off") {
    if (acPower) { acPower = false; sendIRState(true); }
    publishACState();
  }
  else if (lowerCmd == "raw") {
    sendRawSignal();
  }
  else if (lowerCmd == "cool") {
    currentMode = kDaikin64Cool;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "heat") {
    currentMode = kDaikin64Heat;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "dry") {
    currentMode = kDaikin64Dry;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "fan") {
    currentMode = kDaikin64Fan;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "fauto") {
    currentFan = kDaikin64FanAuto;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "f1") {
    currentFan = kDaikin64FanLow;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "f2") {
    currentFan = kDaikin64FanMed;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "f3") {
    currentFan = kDaikin64FanHigh;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "fturbo") {
    currentFan = kDaikin64FanTurbo;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "fquiet") {
    currentFan = kDaikin64FanQuiet;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "swing1") {
    currentSwing = true;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd == "swing0") {
    currentSwing = false;
    sendIRState(false);
    publishACState();
  }
  else if (lowerCmd.startsWith("t")) {
    int t = lowerCmd.substring(1).toInt();
    if (t >= 16 && t <= 30) {
      currentTemp = t;
      sendIRState(false);
      publishACState();
    } else {
      Serial.println("[LỖI]: Nhiệt độ hợp lệ: 16..30");
    }
  }
  else if (lowerCmd == "status") {
    printStatus();
  }
  else if (lowerCmd == "reset_wifi") {
    clearStoredWiFi();
    Serial.println("[RESET]: Đã xóa cấu hình WiFi/MQTT trong NVS.");
  }
  else if (lowerCmd == "mdns") {
    Serial.println("[mDNS]: Kích hoạt tìm lại Broker...");
    mqttBrokerFound = false;
    if (discoverMQTT()) connectMQTT();
  }
  else {
    Serial.println("Lệnh Serial không hợp lệ. Các lệnh hỗ trợ:");
    Serial.println("  power, on, off, t16..t30, cool, heat, dry, fan,");
    Serial.println("  fauto, f1, f2, f3, fturbo, fquiet, swing1, swing0,");
    Serial.println("  raw, status, reset_wifi, mdns");
  }
}

void printStatus() {
  Serial.println();
  Serial.println("================= TRẠNG THÁI HIỆN TẠI =================");
  Serial.printf("Device ID     : %s\n", DEVICE_ID);
  Serial.printf("Node ID       : %s\n", currentNodeId.c_str());
  Serial.printf("Điều hòa (AC) : %s\n", acPower ? "ĐANG BẬT" : "ĐANG TẮT");
  Serial.printf("Nhiệt độ      : %d °C\n", currentTemp);
  Serial.printf("Chế độ (Mode) : %s\n", modeToString(currentMode));
  Serial.printf("Quạt (Fan)    : %s\n", fanToString(currentFan));
  Serial.printf("Đảo gió (Swing): %s\n", currentSwing ? "BẬT" : "TẮT");
  Serial.printf("WiFi          : %s (IP: %s, RSSI: %d dBm)\n",
                (WiFi.status() == WL_CONNECTED) ? "ĐÃ KẾT NỐI" : "CHƯA KẾT NỐI",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  Serial.printf("MQTT Broker   : %s:%d (%s)\n",
                mqttBrokerIP.toString().c_str(),
                mqttBrokerPort,
                mqttClient.connected() ? "ĐÃ KẾT NỐI" : "MẤT KẾT NỐI");
  Serial.printf("BLE Status    : %s\n", bleClientConnected ? "APP ĐÃ KẾT NỐI" : "ĐANG PHÁT QUẢNG BÁ");
  Serial.println("========================================================");
}

// ============================================================
// ARDUINO SETUP & LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================================");
  Serial.println("   HESTA SMART HOME - DAIKIN64 BLE & MQTT CONTROLLER    ");
  Serial.printf("   Device ID: %s\n", DEVICE_ID);
  Serial.println("========================================================");

  ac.begin();
  irsend.begin();

  loadStoredACState();
  loadStoredWiFi();
  loadStoredMQTT();

  startBLE();

  if (wifiSSID.length() > 0) {
    shouldConnectWiFi = true;
  }

  Serial.println("Sẵn sàng! Có thể điều khiển qua Backend MQTT, BLE App hoặc Serial.");
  printStatus();
}

void loop() {
  if (shouldConnectWiFi) {
    handleWiFiProvisioning();
  }

  if (WiFi.status() != WL_CONNECTED && wifiSSID.length() > 0) {
    unsigned long now = millis();
    if (now - lastWiFiReconnectAttempt > 15000) {
      lastWiFiReconnectAttempt = now;
      Serial.println("[WiFi] Mất kết nối, đang thử kết nối lại...");
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt > 10000) {
        lastMqttReconnectAttempt = now;
        Serial.println("[MQTT] Đang thử kết nối lại MQTT...");
        connectMQTT();
      }
    } else {
      mqttClient.loop();

      unsigned long now = millis();
      if (now - lastTelemetryPublish > 60000) {
        lastTelemetryPublish = now;
        publishTelemetry();
      }
    }
  }

  handleSerial();

  delay(10);
}
