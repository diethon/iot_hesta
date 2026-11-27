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
#include "HestaOta.h"

// ============================================================
// HESTA / SYNA SMART HOME - FAN & MOTOR CONTROLLER
// 
// Tính năng:
// 1. Nhập cấu hình Wi-Fi qua App di động bằng giao thức BLE.
// 2. Tự động lưu SSID/Password vào bộ nhớ NVS (Preferences) 
//    và tự động kết nối lại khi khởi động.
// 3. Tự động tìm MQTT Broker qua mDNS (_syna-mqtt._tcp) 
//    hoặc fallback về IP cấu hình sẵn.
// 4. Tự động kết nối và duy trì kết nối MQTT.
// 5. Backend có thể điều khiển bật/tắt Quạt và Motor độc lập hoặc cùng lúc.
// 6. Tự động phản hồi trạng thái (State) và xác nhận (ACK) về Backend.
// 7. Giữ lại điều khiển thủ công qua Serial Monitor ('1','0','3','2','A','B').
// ============================================================

// ============================================================
// HARDWARE PIN CONFIGURATION
// ============================================================
#define MOTOR_RELAY 18
#define FAN_RELAY   19

// Module relay thông dụng thường kích mức LOW.
// Nếu relay của bạn kích mức HIGH, hãy đổi hai giá trị này.
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ============================================================
// NODE & DEVICE IDENTIFIERS (HESTA BACKEND)
// ============================================================
// Định danh Node và Device tương ứng trong Database backend:
#define NODE_ID          "bbb20810-1591-41ca-b3be-daea1466fab5"
#define FAN_DEVICE_ID    "6643cf42-bb8e-434e-a440-743afe06c457"
#define MOTOR_DEVICE_ID  "664017ce-c045-4b0e-9a26-f9f95f996803"
#define FIRMWARE_VERSION "1.0.0"

// ============================================================
// BLE PROVISIONING CONFIGURATION (SYNA MOBILE APP)
// ============================================================
#define BLE_DEVICE_NAME        "SYNA-FAN-MOTOR"
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID     "12345678-1234-1234-1234-123456789001"

// ============================================================
// MQTT & mDNS CONFIGURATION
// ============================================================
#define MQTT_DISCOVERY_SERVICE  "_syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "_tcp"
#define DEFAULT_MQTT_PORT       1883
#define FALLBACK_MQTT_IP        "192.168.137.107"

// ============================================================
// TOPIC TEMPLATES
// ============================================================
// Command topics:
// - hesta/nodes/{NODE_ID}/devices/+/command
// - hesta/nodes/{NODE_ID}/command
const String MQTT_SUB_DEVICES_COMMAND = "hesta/nodes/" + String(NODE_ID) + "/devices/+/command";
const String MQTT_SUB_NODE_COMMAND    = "hesta/nodes/" + String(NODE_ID) + "/command";

// State topics:
const String FAN_STATE_TOPIC   = "hesta/nodes/" + String(NODE_ID) + "/devices/" + String(FAN_DEVICE_ID) + "/state";
const String MOTOR_STATE_TOPIC = "hesta/nodes/" + String(NODE_ID) + "/devices/" + String(MOTOR_DEVICE_ID) + "/state";

// ACK topics:
const String FAN_ACK_TOPIC   = "hesta/nodes/" + String(NODE_ID) + "/devices/" + String(FAN_DEVICE_ID) + "/ack";
const String MOTOR_ACK_TOPIC = "hesta/nodes/" + String(NODE_ID) + "/devices/" + String(MOTOR_DEVICE_ID) + "/ack";

// ============================================================
// GLOBAL OBJECTS & VARIABLES
// ============================================================
Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

BLEServer* bleServer = nullptr;
BLECharacteristic* commCharacteristic = nullptr;

bool bleClientConnected = false;
volatile bool shouldConnectWiFi = false;

String wifiSSID = "";
String wifiPassword = "";

bool mdnsStarted = false;
bool mqttBrokerFound = false;
IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = DEFAULT_MQTT_PORT;

bool fanState = false;
bool motorState = false;

unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastDiscoveryAttempt = 0;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void loadStoredWiFi();
void saveStoredWiFi(const String& ssid, const String& password);
void clearStoredWiFi();
void loadStoredMQTT();
void saveStoredMQTT(const String& host, uint16_t port);

void startBLE();
void notifyBLE(const String& message);

void handleWiFiProvisioning();
bool discoverMQTT();
bool connectMQTT();

void setFan(bool on, const char* commandId = nullptr);
void setMotor(bool on, const char* commandId = nullptr);

void publishFanState(const char* commandId = nullptr);
void publishMotorState(const char* commandId = nullptr);

void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleSerial();
void printStatus();

// ============================================================
// BLE CALLBACKS
// ============================================================
class SynaBLEServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleClientConnected = true;
    Serial.println("[BLE] App đã kết nối!");
  }

  void onDisconnect(BLEServer* server) override {
    bleClientConnected = false;
    Serial.println("[BLE] App đã ngắt kết nối. Đang phát lại quảng bá...");
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
      Serial.println("[BLE LỖI]: Chuỗi JSON không hợp lệ");
      return;
    }

    const char* action = doc["action"] | "";

    // 1. App gửi thông tin cấu hình WiFi
    if (String(action) == "set_wifi") {
      const char* ssid = doc["ssid"] | "";
      const char* password = doc["password"] | "";

      if (strlen(ssid) == 0) {
        notifyBLE("{\"status\":\"error\",\"message\":\"ssid_required\"}");
        return;
      }

      wifiSSID = String(ssid);
      wifiPassword = String(password);

      // Nếu App có gửi kèm MQTT Broker host trực tiếp
      if (doc.containsKey("mqtt_host")) {
        const char* mHost = doc["mqtt_host"] | "";
        int mPort = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
        if (strlen(mHost) > 0) {
          if (mqttBrokerIP.fromString(mHost)) {
            mqttBrokerPort = mPort;
            mqttBrokerFound = true;
            saveStoredMQTT(mHost, mPort);
            Serial.print("[BLE]: Nhận cấu hình MQTT từ App: ");
            Serial.print(mqttBrokerIP);
            Serial.print(":");
            Serial.println(mqttBrokerPort);
          }
        }
      }

      shouldConnectWiFi = true;

      notifyBLE("{\"status\":\"connecting_wifi\",\"message\":\"wifi_credentials_received\"}");
      Serial.println("[BLE]: Đã nhận SSID và Password, chuẩn bị kết nối WiFi...");
    }
    // 2. Lệnh xoá cấu hình WiFi đã lưu
    else if (String(action) == "reset_wifi") {
      clearStoredWiFi();
      wifiSSID = "";
      wifiPassword = "";
      mqttBrokerFound = false;
      WiFi.disconnect(true);
      mqttClient.disconnect();
      notifyBLE("{\"status\":\"wifi_reset\",\"message\":\"WiFi credentials cleared\"}");
      Serial.println("[BLE]: Đã xoá thông tin WiFi đã lưu");
    }
    // 3. Yêu cầu lấy thông tin trạng thái
    else if (String(action) == "get_info") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument info;
#else
      DynamicJsonDocument info(512);
#endif
      info["nodeId"] = NODE_ID;
      info["fan"] = fanState;
      info["motor"] = motorState;
      info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      info["ip"] = WiFi.localIP().toString();
      info["mqtt_connected"] = mqttClient.connected();

      char buf[256];
      serializeJson(info, buf);
      notifyBLE(String(buf));
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
  commCharacteristic->setValue("{\"status\":\"ready\",\"device\":\"SYNA-FAN-MOTOR\"}");
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.print("[BLE] Đang phát sóng với tên: ");
  Serial.println(BLE_DEVICE_NAME);
}

void notifyBLE(const String& message) {
  if (commCharacteristic != nullptr && bleClientConnected) {
    commCharacteristic->setValue(message.c_str());
    commCharacteristic->notify();
    Serial.print("[BLE GỬI APP]: ");
    Serial.println(message);
  }
}

// ============================================================
// NVS PREFERENCES (LƯU & ĐỌC THÔNG TIN WIFI)
// ============================================================
void loadStoredWiFi() {
  preferences.begin("wifi", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  if (wifiSSID.length() > 0) {
    Serial.print("[NVS] Đã tìm thấy WiFi đã lưu: ");
    Serial.println(wifiSSID);
  } else {
    Serial.println("[NVS] Chưa có cấu hình WiFi được lưu.");
  }
}

void saveStoredWiFi(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  Serial.println("[NVS] Đã lưu thông tin WiFi vào bộ nhớ flash.");
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
      Serial.print("[NVS] Đã nạp MQTT Broker đã lưu: ");
      Serial.print(mqttBrokerIP);
      Serial.print(":");
      Serial.println(mqttBrokerPort);
    }
  }
}

void saveStoredMQTT(const String& host, uint16_t port) {
  preferences.begin("mqtt", false);
  preferences.putString("host", host);
  preferences.putUShort("port", port);
  preferences.end();
  Serial.println("[NVS] Đã lưu thông tin MQTT Broker vào bộ nhớ flash.");
}

void clearStoredWiFi() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  preferences.begin("mqtt", false);
  preferences.clear();
  preferences.end();

  mqttBrokerFound = false;
  Serial.println("[NVS] Đã xoá toàn bộ cấu hình WiFi & MQTT trong flash.");
}

// ============================================================
// WIFI PROVISIONING & CONNECTION
// ============================================================
void handleWiFiProvisioning() {
  shouldConnectWiFi = false;

  notifyBLE("{\"status\":\"connecting_wifi\"}");

  mqttClient.disconnect();
  WiFi.disconnect(true);
  delay(200);

  Serial.println();
  Serial.println("======================================");
  Serial.print("[WiFi] Đang kết nối tới: ");
  Serial.println(wifiSSID);
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
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());

    // Lưu lại thông tin WiFi vào flash
    saveStoredWiFi(wifiSSID, wifiPassword);

    // Báo App biết đã kết nối WiFi
    String okJson = "{\"status\":\"wifi_connected\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    notifyBLE(okJson);

    // Đợi 1s cho mạng ổn định rồi tìm broker qua mDNS
    delay(1000);
    mqttBrokerFound = false;
    if (discoverMQTT()) {
      if (connectMQTT()) {
        notifyBLE("{\"status\":\"mqtt_connected\"}");
      }
    }
  } else {
    Serial.println("[WiFi] KẾT NỐI THẤT BẠI. Vui lòng kiểm tra lại SSID/Mật khẩu.");
    notifyBLE("{\"status\":\"wifi_failed\",\"error\":\"Sai mật khẩu hoặc sóng yếu\"}");
    WiFi.disconnect(true);
  }
}

// ============================================================
// MQTT DISCOVERY (mDNS AUTO-DISCOVERY)
// ============================================================
bool discoverMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  Serial.println();
  Serial.println("======================================");
  Serial.println("[mDNS] Đang tìm kiếm MQTT broker qua DNS-SD...");
  Serial.println("       Service: _syna-mqtt._tcp.local");
  Serial.println("======================================");

  MDNS.end();
  String mdnsHost = "syna-fan-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!MDNS.begin(mdnsHost.c_str())) {
    Serial.println("[mDNS] Không thể khởi tạo mDNS responder");
    return false;
  }

  // Tìm dịch vụ _syna-mqtt._tcp (chuẩn Avahi Linux)
  int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);
  if (count <= 0) {
    count = MDNS.queryService("syna-mqtt", "tcp");
  }

  Serial.print("[mDNS] Số lượng broker tìm thấy: ");
  Serial.println(count);

  if (count > 0) {
    for (int i = 0; i < count; i++) {
      IPAddress discoveredIP = MDNS.address(i);
      uint16_t discoveredPort = MDNS.port(i);

      Serial.print("[mDNS] Broker #"); Serial.println(i + 1);
      Serial.print("       Host: "); Serial.println(MDNS.hostname(i));
      Serial.print("       IP  : "); Serial.println(discoveredIP);
      Serial.print("       Port: "); Serial.println(discoveredPort);

      if (discoveredIP != IPAddress(0, 0, 0, 0) && discoveredPort > 0) {
        mqttBrokerIP = discoveredIP;
        mqttBrokerPort = discoveredPort;
        mqttBrokerFound = true;

        Serial.println("[mDNS] >>> TÌM THẤY MQTT BROKER THỰC TẾ THÀNH CÔNG! <<<");
        Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
        Serial.print("       Port: "); Serial.println(mqttBrokerPort);

        // Lưu vào flash NVS để ghi nhớ cho lần sau
        saveStoredMQTT(mqttBrokerIP.toString(), mqttBrokerPort);
        return true;
      }
    }
  }

  Serial.println("[mDNS] Chưa tìm thấy MQTT broker qua mDNS.");

  // Fallback 1: Sử dụng cấu hình IP broker đã lưu trước đó nếu có
  if (mqttBrokerFound && mqttBrokerIP != IPAddress(0, 0, 0, 0)) {
    Serial.print("[mDNS] Sử dụng IP MQTT Broker đã lưu trong NVS/App: ");
    Serial.print(mqttBrokerIP);
    Serial.print(":");
    Serial.println(mqttBrokerPort);
    return true;
  }

  // Fallback 2: Ưu tiên sử dụng IP Broker cấu hình sẵn (192.168.137.107)
  if (mqttBrokerIP.fromString(FALLBACK_MQTT_IP)) {
    mqttBrokerPort = DEFAULT_MQTT_PORT;
    mqttBrokerFound = true;
    Serial.println("[mDNS] >>> FALLBACK: Ưu tiên sử dụng IP Broker cấu hình sẵn (" FALLBACK_MQTT_IP ") <<<");
    Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
    Serial.print("       Port: "); Serial.println(mqttBrokerPort);
    return true;
  }

  // Fallback 3: Tự động dùng Gateway IP (IP của máy tính phát Hotspot / Router)
  IPAddress gw = WiFi.gatewayIP();
  if (gw != IPAddress(0, 0, 0, 0)) {
    mqttBrokerIP = gw;
    mqttBrokerPort = DEFAULT_MQTT_PORT;
    mqttBrokerFound = true;
    Serial.println("[mDNS] >>> FALLBACK: Tự động sử dụng Gateway IP làm MQTT Broker <<<");
    Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
    Serial.print("       Port: "); Serial.println(mqttBrokerPort);
    return true;
  }

  return false;
}

// ============================================================
// CONNECT MQTT
// ============================================================
bool connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  // Nếu chưa tìm thấy broker thực tế, thử tìm qua mDNS
  if (!mqttBrokerFound) {
    if (!discoverMQTT()) {
      return false;
    }
  }

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024); // Đảm bảo buffer đủ lớn cho JSON từ backend

  String clientId = "SYNA-FAN-MOTOR-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.println();
  Serial.println("======================================");
  Serial.print("[MQTT] Đang kết nối tới Broker ");
  Serial.print(mqttBrokerIP);
  Serial.print(":");
  Serial.println(mqttBrokerPort);
  Serial.println("======================================");

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("[MQTT] KẾT NỐI MQTT THÀNH CÔNG!");

    // Subscribe nhận lệnh từ backend
    mqttClient.subscribe(MQTT_SUB_DEVICES_COMMAND.c_str());
    mqttClient.subscribe(MQTT_SUB_NODE_COMMAND.c_str());

    Serial.print("[MQTT] Subscribed topic: ");
    Serial.println(MQTT_SUB_DEVICES_COMMAND);
    Serial.print("[MQTT] Subscribed topic: ");
    Serial.println(MQTT_SUB_NODE_COMMAND);

    // Gửi thông báo trạng thái ban đầu lên Backend
    publishFanState();
    publishMotorState();

    // Thông báo cho app nếu app đang mở
    notifyBLE("{\"status\":\"mqtt_connected\"}");
    hestaOtaOnMqttConnected();
    return true;
  } else {
    Serial.print("[MQTT] Kết nối thất bại, mã lỗi rc=");
    Serial.println(mqttClient.state());

    // Nếu không kết nối được tới IP này, reset cờ để mDNS tìm lại IP broker thực tế
    mqttBrokerFound = false;
    return false;
  }
}

// ============================================================
// RELAY CONTROL & STATE PUBLISH
// ============================================================
void setFan(bool on, const char* commandId) {
  fanState = on;
  digitalWrite(FAN_RELAY, fanState ? RELAY_ON : RELAY_OFF);
  Serial.print("[RELAY] QUẠT: ");
  Serial.println(fanState ? "BẬT (ON)" : "TẮT (OFF)");
  publishFanState(commandId);
}

void setMotor(bool on, const char* commandId) {
  motorState = on;
  digitalWrite(MOTOR_RELAY, motorState ? RELAY_ON : RELAY_OFF);
  Serial.print("[RELAY] MOTOR: ");
  Serial.println(motorState ? "BẬT (ON)" : "TẮT (OFF)");
  publishMotorState(commandId);
}

void publishFanState(const char* commandId) {
  if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif

  doc["deviceId"] = FAN_DEVICE_ID;
  doc["power"] = fanState;
  doc["state"] = fanState ? "ON" : "OFF";

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(FAN_STATE_TOPIC.c_str(), buf, true);
  Serial.print("[MQTT STATE -> FAN]: ");
  Serial.println(buf);

  // Gửi ACK về Backend nếu lệnh có commandId
  if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument ackDoc;
#else
    DynamicJsonDocument ackDoc(256);
#endif
    ackDoc["commandId"] = commandId;
    ackDoc["deviceId"] = FAN_DEVICE_ID;
    ackDoc["status"] = "SUCCESS";
    JsonObject st = ackDoc["state"].to<JsonObject>();
    st["power"] = fanState;
    st["state"] = fanState ? "ON" : "OFF";

    char ackBuf[256];
    serializeJson(ackDoc, ackBuf);
    mqttClient.publish(FAN_ACK_TOPIC.c_str(), ackBuf);
    Serial.print("[MQTT ACK -> FAN]: ");
    Serial.println(ackBuf);
  }
}

void publishMotorState(const char* commandId) {
  if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif

  doc["deviceId"] = MOTOR_DEVICE_ID;
  doc["power"] = motorState;
  doc["state"] = motorState ? "ON" : "OFF";

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(MOTOR_STATE_TOPIC.c_str(), buf, true);
  Serial.print("[MQTT STATE -> MOTOR]: ");
  Serial.println(buf);

  // Gửi ACK về Backend nếu lệnh có commandId
  if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument ackDoc;
#else
    DynamicJsonDocument ackDoc(256);
#endif
    ackDoc["commandId"] = commandId;
    ackDoc["deviceId"] = MOTOR_DEVICE_ID;
    ackDoc["status"] = "SUCCESS";
    JsonObject st = ackDoc["state"].to<JsonObject>();
    st["power"] = motorState;
    st["state"] = motorState ? "ON" : "OFF";

    char ackBuf[256];
    serializeJson(ackDoc, ackBuf);
    mqttClient.publish(MOTOR_ACK_TOPIC.c_str(), ackBuf);
    Serial.print("[MQTT ACK -> MOTOR]: ");
    Serial.println(ackBuf);
  }
}

// ============================================================
// MQTT CALLBACK (XỬ LÝ LỆNH TỪ BACKEND / APP)
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (hestaOtaHandleMqttMessage(topic, payload, length)) {
    return;
  }

  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.println();
  Serial.println("======================================");
  Serial.print("[MQTT NHẬN LỆNH] Topic: ");
  Serial.println(topic);
  Serial.print("[MQTT NHẬN LỆNH] Payload: ");
  Serial.println(message);
  Serial.println("======================================");

  String topicStr = String(topic);

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(1024);
#endif

  DeserializationError error = deserializeJson(doc, message);

  if (!error) {
    const char* commandId = doc["commandId"] | "";
    const char* action    = doc["action"] | "";
    const char* target    = doc["target"] | "";
    const char* devId     = doc["deviceId"] | "";

    bool targetIsFan = false;
    bool targetIsMotor = false;
    bool targetBoth = false;

    // 1. Phân loại thiết bị qua Topic
    if (topicStr.indexOf(FAN_DEVICE_ID) >= 0 || topicStr.indexOf("/fan/") >= 0) {
      targetIsFan = true;
    } else if (topicStr.indexOf(MOTOR_DEVICE_ID) >= 0 || topicStr.indexOf("/motor/") >= 0) {
      targetIsMotor = true;
    }

    // 2. Phân loại thiết bị qua target/deviceId trong payload
    if (String(target).equalsIgnoreCase("fan") || String(devId).equalsIgnoreCase(FAN_DEVICE_ID)) {
      targetIsFan = true;
    } else if (String(target).equalsIgnoreCase("motor") || String(devId).equalsIgnoreCase(MOTOR_DEVICE_ID)) {
      targetIsMotor = true;
    }

    // 3. Phân loại qua action (ví dụ: TURN_ON_FAN, TURN_OFF_MOTOR, TURN_ON_ALL...)
    String act = String(action);
    act.toUpperCase();
    if (act.indexOf("FAN") >= 0) targetIsFan = true;
    if (act.indexOf("MOTOR") >= 0) targetIsMotor = true;
    if (act.indexOf("ALL") >= 0) targetBoth = true;

    // 4. Xác định hành động Bật hay Tắt
    bool turnOn = false;
    bool turnOff = false;

    if (act == "TURN_ON" || act == "POWER_ON" || act == "ON" || act == "OPEN" ||
        act == "FAN_ON" || act == "MOTOR_ON" || act == "ALL_ON") {
      turnOn = true;
    } else if (act == "TURN_OFF" || act == "POWER_OFF" || act == "OFF" || act == "CLOSE" ||
               act == "STOP" || act == "FAN_OFF" || act == "MOTOR_OFF" || act == "ALL_OFF") {
      turnOff = true;
    } else if (doc.containsKey("power")) {
      if (doc["power"].is<bool>()) {
        if (doc["power"].as<bool>()) turnOn = true; else turnOff = true;
      } else if (doc["power"].is<const char*>()) {
        String p = doc["power"].as<const char*>();
        p.toUpperCase();
        if (p == "ON" || p == "TRUE" || p == "1") turnOn = true; else turnOff = true;
      }
    } else if (act == "SET_FAN") {
      const char* fanSpeed = doc["fan"] | "";
      if (String(fanSpeed).equalsIgnoreCase("OFF")) turnOff = true; else turnOn = true;
    }

    // 5. Kiểm tra trường riêng biệt trong JSON nếu có {"fan": true, "motor": false}
    if (doc.containsKey("fan")) {
      bool f = doc["fan"].as<bool>();
      setFan(f, commandId);
    }
    if (doc.containsKey("motor")) {
      bool m = doc["motor"].as<bool>();
      setMotor(m, commandId);
    }

    // 6. Thực hiện đóng ngắt relay theo đối tượng
    if (targetBoth) {
      if (turnOn) { setFan(true, commandId); setMotor(true, commandId); }
      if (turnOff) { setFan(false, commandId); setMotor(false, commandId); }
    } else if (targetIsFan) {
      if (turnOn) setFan(true, commandId);
      if (turnOff) setFan(false, commandId);
    } else if (targetIsMotor) {
      if (turnOn) setMotor(true, commandId);
      if (turnOff) setMotor(false, commandId);
    } else if (!doc.containsKey("fan") && !doc.containsKey("motor")) {
      // Nếu không chỉ định thiết bị cụ thể ở topic/payload, áp dụng cho cả hai
      if (turnOn) { setFan(true, commandId); setMotor(true, commandId); }
      if (turnOff) { setFan(false, commandId); setMotor(false, commandId); }
    }
  } else {
    // Xử lý chuỗi plain-text đơn giản nếu gửi test trực tiếp từ MQTT client khác
    String raw = String(message);
    raw.trim();
    if (raw == "1") setMotor(true);
    else if (raw == "0") setMotor(false);
    else if (raw == "3") setFan(true);
    else if (raw == "2") setFan(false);
    else if (raw.equalsIgnoreCase("A")) { setMotor(true); setFan(true); }
    else if (raw.equalsIgnoreCase("B")) { setMotor(false); setFan(false); }
  }
}

// ============================================================
// SERIAL COMMANDS (ĐIỀU KHIỂN & DEBUG TRỰC TIẾP)
// ============================================================
void handleSerial() {
  if (!Serial.available()) return;

  char c = Serial.read();

  switch (c) {
    case '1':
      setMotor(true);
      break;
    case '0':
      setMotor(false);
      break;
    case '3':
      setFan(true);
      break;
    case '2':
      setFan(false);
      break;
    case 'A':
    case 'a':
      setMotor(true);
      setFan(true);
      break;
    case 'B':
    case 'b':
      setMotor(false);
      setFan(false);
      break;
    case 'M':
    case 'm':
      Serial.println("[mDNS] Kích hoạt tìm kiếm lại MQTT Broker qua mDNS...");
      mqttBrokerFound = false;
      if (discoverMQTT()) {
        connectMQTT();
      }
      break;
    case 'R':
    case 'r':
      Serial.println("[RESET] Đang xoá cấu hình WiFi & MQTT trong NVS...");
      clearStoredWiFi();
      Serial.println("[RESET] Xoá thành công. Khởi động lại để test provisioning.");
      break;
    case 'S':
    case 's':
      printStatus();
      break;
  }
}

void printStatus() {
  Serial.println();
  Serial.println("================= TRẠNG THÁI HIỆN TẠI =================");
  Serial.print("Node ID       : "); Serial.println(NODE_ID);
  Serial.print("Fan Device ID : "); Serial.println(FAN_DEVICE_ID);
  Serial.print("Motor Dev ID  : "); Serial.println(MOTOR_DEVICE_ID);
  Serial.print("Quạt          : "); Serial.println(fanState ? "ĐANG BẬT" : "ĐANG TẮT");
  Serial.print("Motor         : "); Serial.println(motorState ? "ĐANG BẬT" : "ĐANG TẮT");
  Serial.print("WiFi          : "); 
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ĐÃ KẾT NỐI (SSID: ");
    Serial.print(WiFi.SSID());
    Serial.print(", IP: ");
    Serial.print(WiFi.localIP());
    Serial.println(")");
  } else {
    Serial.println("CHƯA KẾT NỐI");
  }
  Serial.print("MQTT Broker   : ");
  if (mqttClient.connected()) {
    Serial.print("ĐÃ KẾT NỐI (");
    Serial.print(mqttBrokerIP);
    Serial.print(":");
    Serial.print(mqttBrokerPort);
    Serial.println(")");
  } else {
    Serial.print("CHƯA KẾT NỐI (Broker IP: ");
    Serial.print(mqttBrokerFound ? mqttBrokerIP.toString() : "Đang tìm qua mDNS...");
    Serial.println(")");
  }
  Serial.print("BLE App       : "); Serial.println(bleClientConnected ? "ĐANG KẾT NỐI" : "ĐANG CHỜ KẾT NỐI");
  Serial.println("========================================================");
}

// ============================================================
// SETUP
// ============================================================
void preOtaHardwareSafeStop() {
  setMotor(false, nullptr);
  if (bleServer != nullptr && BLEDevice::getAdvertising() != nullptr) {
    BLEDevice::getAdvertising()->stop();
  }
}

void postOtaFailResume() {
  if (bleServer != nullptr && BLEDevice::getAdvertising() != nullptr) {
    BLEDevice::startAdvertising();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================================");
  Serial.println("   HESTA / SYNA SMART HOME - FAN & MOTOR CONTROLLER     ");
  Serial.println("========================================================");

  // Khởi tạo chân Relay
  pinMode(MOTOR_RELAY, OUTPUT);
  pinMode(FAN_RELAY, OUTPUT);
  digitalWrite(MOTOR_RELAY, RELAY_OFF);
  digitalWrite(FAN_RELAY, RELAY_OFF);

  // Menu phím Serial
  Serial.println("Phím bấm Serial Monitor:");
  Serial.println("  1: Bat motor    | 0: Tat motor");
  Serial.println("  3: Bat quat     | 2: Tat quat");
  Serial.println("  A: Bat ca hai   | B: Tat ca hai");
  Serial.println("  M: Quet lai MQTT Broker qua mDNS");
  Serial.println("  S: Xem trang thai he thong");
  Serial.println("  R: Xoa WiFi & MQTT da luu trong flash");
  Serial.println("--------------------------------------------------------");

  // Khởi tạo BLE trước để App luôn có thể scan và kết nối
  startBLE();

  mqttClient.setBufferSize(1024);
  hestaOtaInit(&mqttClient, NODE_ID, FIRMWARE_VERSION, preOtaHardwareSafeStop, postOtaFailResume);

  // Đọc thông tin WiFi và MQTT từ flash (NVS)
  loadStoredWiFi();
  loadStoredMQTT();

  // Nếu đã có cấu hình WiFi, tự động kết nối
  if (wifiSSID.length() > 0) {
    Serial.println("[WiFi] Đang tự động kết nối bằng WiFi đã lưu...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    unsigned long startAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAt < 10000) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Tự động kết nối thành công!");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());

      // Đợi 1s cho mạng ổn định trước khi tìm broker qua mDNS
      delay(1000);
      if (discoverMQTT()) {
        connectMQTT();
      }
    } else {
      Serial.println("[WiFi] Không thể kết nối với WiFi đã lưu. Đang chờ nhập qua App...");
    }
  } else {
    Serial.println("[WiFi] Chưa có thông tin WiFi. Hãy mở App SYNA để cài đặt.");
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // 1. Xử lý yêu cầu kết nối WiFi khi App gửi cấu hình qua BLE
  if (shouldConnectWiFi) {
    handleWiFiProvisioning();
  }

  // 2. Tự động kết nối lại WiFi nếu bị mất sóng
  if (wifiSSID.length() > 0 && WiFi.status() != WL_CONNECTED && !shouldConnectWiFi) {
    unsigned long now = millis();
    if (now - lastWiFiReconnectAttempt >= 10000) {
      lastWiFiReconnectAttempt = now;
      Serial.println("[WiFi] Mất kết nối WiFi, đang thử kết nối lại...");
      mqttClient.disconnect();
      mqttBrokerFound = false;
      WiFi.reconnect();
    }
  }

  // 3. Duy trì và kết nối lại MQTT
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttBrokerFound) {
      // Nếu chưa tìm thấy broker qua mDNS, thử tìm lại định kỳ mỗi 5 giây
      unsigned long now = millis();
      if (now - lastDiscoveryAttempt >= 5000) {
        lastDiscoveryAttempt = now;
        Serial.println("[mDNS] Định kỳ tìm kiếm lại MQTT Broker...");
        if (discoverMQTT()) {
          connectMQTT();
        }
      }
    } else if (!mqttClient.connected()) {
      // Đã có IP broker nhưng đang mất kết nối, thử kết nối lại mỗi 5 giây
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt >= 5000) {
        lastMqttReconnectAttempt = now;
        Serial.println("[MQTT] Mất kết nối MQTT, đang thử kết nối lại...");
        connectMQTT();
      }
    } else {
      mqttClient.loop();
      hestaOtaLoop();
    }
  }

  // 4. Đọc phím điều khiển qua Serial Monitor
  handleSerial();

  delay(10);
}