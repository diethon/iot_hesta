/*
 * ====================================================================================================
 * DỰ ÁN: HESTA / SYNA SMART HOME
 * THIẾT BỊ: SMART-SLUG (Ổ CẮM THÔNG MINH / RELAY SWITCH)
 * PHẦN CỨNG: ESP32 / ESP32-CAM / ESP32-S3
 * ====================================================================================================
 *
 * TÍNH NĂNG CHÍNH:
 * 1. QUY TRÌNH ADD DEVICE (BLE PROVISIONING):
 *    - Quét & Kết nối BLE hiển thị tên thiết bị / dịch vụ: "Smart Slug"
 *    - Nhận cấu hình Wi-Fi (SSID, Password) từ App Syna qua BLE Characteristic.
 *    - Phản hồi tiến trình kết nối (connecting_wifi, wifi_connected, mdns_discovered, mqtt_connected, ready).
 * 2. TỰ ĐỘNG KẾT NỐI WIFI & DÒ TÌM MQTT BROKER:
 *    - Tự động lưu và khôi phục thông tin Wi-Fi, MQTT trong bộ nhớ Flash (NVS Preferences).
 *    - Tự động tìm kiếm Mosquitto Broker qua mDNS (_syna-mqtt._tcp) và kết nối tức thì.
 *    - Tự phục hồi kết nối (Auto Reconnect) khi mất sóng Wi-Fi hoặc mất kết nối Broker.
 * 3. ĐIỀU KHIỂN THIẾT BỊ (RELAY):
 *    - Đóng / Ngắt nguồn Relay cho sản phẩm Smart-Slug (GPIO 13).
 *    - Điều khiển qua MQTT lệnh Backend: POWER_ON, POWER_OFF, TOGGLE, SET_POWER.
 *    - Hỗ trợ nút nhấn vật lý (BOOT pin hoặc chân ngoài) để bật/tắt tại chỗ & nhấn giữ 5s để Reset.
 *    - Giao diện Serial Monitor (115200 baud) để test trực tiếp bằng bàn phím.
 * ====================================================================================================
 */

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

// ====================================================================================================
// 1. CẤU HÌNH ĐỊNH DANH SẢN PHẨM & BACKEND (NODE ID & DEVICE ID ĐỂ TRỐNG)
// ====================================================================================================
// Tên sản phẩm chính thức
#define PRODUCT_NAME        "Smart-Slug"
#define DEVICE_TYPE         "SMART_PLUG"
#define FIRMWARE_VERSION    "1.0.0"

// Tên hiển thị khi quét và kết nối BLE
#define BLE_DEVICE_NAME     "Smart Slug"
#define BLE_SERVICE_NAME    "Smart Slug"

// Node ID và Device ID để trống theo yêu cầu (người dùng có thể để trống hoặc điền sau)
#define DEFAULT_NODE_ID     ""
#define DEFAULT_DEVICE_ID   ""

// ====================================================================================================
// 2. CẤU HÌNH BLE PROVISIONING (CHUẨN SYNA MOBILE APP)
// ====================================================================================================
#define BLE_SERVICE_UUID    "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID  "12345678-1234-1234-1234-123456789001"

// ====================================================================================================
// 3. CẤU HÌNH PHẦN CỨNG (RELAY & NÚT NHẤN)
// ====================================================================================================
// Chân điều khiển Relay (GPIO 13)
#define RELAY_PIN           13

// Mức logic kích Relay (Đa số module relay kích mức LOW)
#define RELAY_ON            LOW
#define RELAY_OFF           HIGH

// Nút nhấn vật lý tại chỗ (Tùy chọn: dùng chân BOOT GPIO 0 tích hợp trên mạch ESP32)
#define ENABLE_PHYSICAL_BUTTON  true
#define BUTTON_PIN              0      // GPIO 0 (Nút BOOT trên ESP32)
#define BUTTON_DEBOUNCE_MS      50
#define BUTTON_LONG_PRESS_MS    5000   // Nhấn giữ 5 giây để Reset cấu hình WiFi/MQTT về xuất xưởng

// ====================================================================================================
// 4. CẤU HÌNH FALLBACK WIFI & MQTT BROKER
// ====================================================================================================
#define FALLBACK_WIFI_SSID      ""
#define FALLBACK_WIFI_PASS      ""

#define FALLBACK_MQTT_IP        "192.168.137.107"
#define DEFAULT_MQTT_PORT       1883
#define MQTT_USER               ""
#define MQTT_PASSWORD           ""

#define MQTT_DISCOVERY_SERVICE  "syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "tcp"

// ====================================================================================================
// 5. BIẾN TOÀN CỤC & ĐỐI TƯỢNG HỆ THỐNG
// ====================================================================================================
Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

BLEServer* bleServer = nullptr;
BLECharacteristic* commCharacteristic = nullptr;

bool bleClientConnected = false;
volatile bool shouldConnectWiFi = false;

// Trạng thái cấu hình động
String wifiSSID     = FALLBACK_WIFI_SSID;
String wifiPassword = FALLBACK_WIFI_PASS;
String currentNodeId   = DEFAULT_NODE_ID;
String currentDeviceId = DEFAULT_DEVICE_ID;

IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = DEFAULT_MQTT_PORT;
bool mqttBrokerFound = false;

// Trạng thái Relay
bool relayState = false; // false = OFF, true = ON

// Quản lý thời gian
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastTelemetryPublish = 0;
const unsigned long TELEMETRY_INTERVAL = 30000; // 30 giây gửi Telemetry một lần

// Quản lý nút nhấn vật lý
#if ENABLE_PHYSICAL_BUTTON
unsigned long buttonPressStartTime = 0;
bool buttonIsPressed = false;
bool longPressTriggered = false;
#endif

// ====================================================================================================
// FORWARD DECLARATIONS (KHAI BÁO NGUYÊN MẪU HÀM)
// ====================================================================================================
void setRelay(bool on);
void toggleRelay();
void loadStoredConfig();
void saveStoredWiFi(const String& ssid, const String& password);
void saveStoredMQTT(const String& host, uint16_t port);
void saveStoredIds(const String& nodeId, const String& deviceId);
void clearStoredConfig();

void startBLE();
void notifyBleStatus(const char* status, const char* key1 = nullptr, const char* val1 = nullptr, const char* key2 = nullptr, const char* val2 = nullptr);

bool connectWiFi();
bool discoverMQTTBroker();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void processCommandPayload(const String& targetDeviceId, const char* payloadStr);

void publishRelayState();
void publishRelayAck(const char* commandId, const char* statusMsg = "SUCCESS");
void publishTelemetry();
void publishRelayStatus(const char* status);

void handlePhysicalButton();
void handleSerialCommands();
void printHelp();
void printSystemStatus();

// ====================================================================================================
// 6. ĐIỀU KHIỂN PHẦN CỨNG RELAY
// ====================================================================================================
void setRelay(bool on) {
    relayState = on;
    digitalWrite(RELAY_PIN, relayState ? RELAY_ON : RELAY_OFF);

    Serial.println();
    Serial.println("--------------------------------------------------");
    Serial.printf("[RELAY] SAN PHAM [%s] -> TRANG THAI: %s (GPIO %d = %s)\n",
                  PRODUCT_NAME,
                  relayState ? "BAT (ON)" : "TAT (OFF)",
                  RELAY_PIN,
                  (digitalRead(RELAY_PIN) == RELAY_ON) ? "ON_LEVEL" : "OFF_LEVEL");
    Serial.println("--------------------------------------------------");

    publishRelayState();
}

void toggleRelay() {
    setRelay(!relayState);
}

// ====================================================================================================
// 7. LƯU TRỮ VÀ NẠP CẤU HÌNH TỪ BỘ NHỚ FLASH (PREFERENCES NVS)
// ====================================================================================================
void loadStoredConfig() {
    preferences.begin("smart_slug_cfg", true);
    wifiSSID     = preferences.getString("ssid", FALLBACK_WIFI_SSID);
    wifiPassword = preferences.getString("pass", FALLBACK_WIFI_PASS);

    // Ưu tiên giá trị macro nếu người dùng gán sau này, ngược lại lấy từ NVS
    if (String(DEFAULT_NODE_ID).length() > 0) {
        currentNodeId = DEFAULT_NODE_ID;
    } else {
        currentNodeId = preferences.getString("node_id", "");
    }

    if (String(DEFAULT_DEVICE_ID).length() > 0) {
        currentDeviceId = DEFAULT_DEVICE_ID;
    } else {
        currentDeviceId = preferences.getString("device_id", "");
    }

    String storedMqtt = preferences.getString("mqtt_ip", "");
    uint16_t storedPort = preferences.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
    preferences.end();

    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[NVS] KHOI PHUC CAU HINH SAN PHAM: %s\n", PRODUCT_NAME);
    Serial.printf(" - Node ID        : %s\n", currentNodeId.length() > 0 ? currentNodeId.c_str() : "(DANG DE TRONG \"\")");
    Serial.printf(" - Device ID      : %s\n", currentDeviceId.length() > 0 ? currentDeviceId.c_str() : "(DANG DE TRONG \"\")");
    Serial.printf(" - WiFi SSID      : %s\n", wifiSSID.length() > 0 ? wifiSSID.c_str() : "(Chua co - Cho Provision qua BLE)");
    if (storedMqtt.length() > 0) {
        mqttBrokerIP.fromString(storedMqtt);
        mqttBrokerPort = storedPort;
        mqttBrokerFound = true;
        Serial.printf(" - Stored MQTT    : %s:%d\n", storedMqtt.c_str(), storedPort);
    }
    Serial.println("==================================================");
}

void saveStoredWiFi(const String& ssid, const String& password) {
    preferences.begin("smart_slug_cfg", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();
    Serial.println("[NVS] Da luu cau hinh WiFi vao bo nho Flash NVS.");
}

void saveStoredMQTT(const String& host, uint16_t port) {
    preferences.begin("smart_slug_cfg", false);
    preferences.putString("mqtt_ip", host);
    preferences.putUShort("mqtt_port", port);
    preferences.end();
    Serial.println("[NVS] Da luu MQTT Broker vao bo nho Flash NVS.");
}

void saveStoredIds(const String& nodeId, const String& deviceId) {
    preferences.begin("smart_slug_cfg", false);
    if (nodeId.length() > 0) preferences.putString("node_id", nodeId);
    if (deviceId.length() > 0) preferences.putString("device_id", deviceId);
    preferences.end();
    Serial.println("[NVS] Da luu Node ID / Device ID vao Flash NVS.");
}

void clearStoredConfig() {
    preferences.begin("smart_slug_cfg", false);
    preferences.clear();
    preferences.end();
    wifiSSID = "";
    wifiPassword = "";
    mqttBrokerFound = false;
    Serial.println("[NVS] Da xoa sach toan bo cau hinh WiFi & MQTT!");
}

// ====================================================================================================
// 8. BLE PROVISIONING (DỊCH VỤ VÀ THÔNG BÁO VỀ APP SYNA)
// ====================================================================================================
void notifyBleStatus(const char* status, const char* key1, const char* val1, const char* key2, const char* val2) {
    if (!bleClientConnected || commCharacteristic == nullptr) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["status"]  = status;
    doc["product"] = PRODUCT_NAME;
    doc["service"] = BLE_SERVICE_NAME;
    doc["device"]  = BLE_DEVICE_NAME;
    if (currentNodeId.length() > 0)   doc["nodeId"]   = currentNodeId;
    if (currentDeviceId.length() > 0) doc["deviceId"] = currentDeviceId;

    if (key1 && val1) doc[key1] = val1;
    if (key2 && val2) doc[key2] = val2;

    String jsonStr;
    serializeJson(doc, jsonStr);

    commCharacteristic->setValue(jsonStr.c_str());
    commCharacteristic->notify();
    delay(40);
    Serial.printf("[BLE NOTIFY -> APP]: %s\n", jsonStr.c_str());
}

class SmartSlugBLEServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        bleClientConnected = true;
        Serial.println();
        Serial.println("==================================================");
        Serial.printf("[BLE] APP DI DONG DA KET NOI! Service: '%s' | Device: '%s'\n", BLE_SERVICE_NAME, BLE_DEVICE_NAME);
        Serial.println("==================================================");
        notifyBleStatus("ble_connected", "device", BLE_DEVICE_NAME, "product", PRODUCT_NAME);
    }

    void onDisconnect(BLEServer* server) override {
        bleClientConnected = false;
        Serial.println();
        Serial.println("[BLE] APP DI DONG DA NGAT KET NOI. Bat lai che do quang ba (Advertising)...");
        BLEDevice::startAdvertising();
    }
};

class SmartSlugBLECharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        String payload = characteristic->getValue().c_str();
        payload.trim();

        if (payload.length() == 0) return;

        Serial.println();
        Serial.println("==================================================");
        Serial.printf("[BLE RX NHAN TU APP]: %s\n", payload.c_str());
        Serial.println("==================================================");

#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(512);
#endif
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[BLE ERROR] Khong the parse JSON: %s\n", err.c_str());
            notifyBleStatus("error", "message", "invalid_json");
            return;
        }

        const char* action = doc["action"] | "";

        // 1. Cấu hình Wi-Fi & Onboarding Thiết bị
        if (strcmp(action, "set_wifi") == 0) {
            const char* ssid = doc["ssid"] | "";
            const char* pass = doc["password"] | "";

            if (strlen(ssid) == 0) {
                notifyBleStatus("wifi_failed", "error", "empty_ssid");
                return;
            }

            wifiSSID = String(ssid);
            wifiPassword = String(pass);
            saveStoredWiFi(wifiSSID, wifiPassword);

            // Cập nhật Node ID / Device ID nếu App gửi kèm trong gói set_wifi
            bool idUpdated = false;
            if (doc.containsKey("node_id")) {
                currentNodeId = (const char*)doc["node_id"];
                idUpdated = true;
            }
            if (doc.containsKey("device_id")) {
                currentDeviceId = (const char*)doc["device_id"];
                idUpdated = true;
            }
            if (idUpdated) {
                saveStoredIds(currentNodeId, currentDeviceId);
            }

            // Nhận MQTT Host nếu App cấu hình trực tiếp
            if (doc.containsKey("mqtt_host")) {
                const char* mHost = doc["mqtt_host"] | "";
                int mPort = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
                if (strlen(mHost) > 0) {
                    mqttBrokerIP.fromString(mHost);
                    mqttBrokerPort = mPort;
                    mqttBrokerFound = true;
                    saveStoredMQTT(String(mHost), mPort);
                }
            }

            notifyBleStatus("connecting_wifi", "ssid", wifiSSID.c_str());
            shouldConnectWiFi = true;
            return;
        }

        // 2. Lấy thông tin thiết bị (get_info)
        if (strcmp(action, "get_info") == 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument info;
#else
            DynamicJsonDocument info(512);
#endif
            info["status"]         = "info";
            info["product"]        = PRODUCT_NAME;
            info["service"]        = BLE_SERVICE_NAME;
            info["name"]           = BLE_DEVICE_NAME;
            info["deviceType"]     = DEVICE_TYPE;
            info["firmware"]       = FIRMWARE_VERSION;
            info["nodeId"]         = currentNodeId;
            info["deviceId"]       = currentDeviceId;
            info["relay"]          = relayState ? "ON" : "OFF";
            info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
            info["ip"]             = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
            info["mqtt_connected"] = mqttClient.connected();

            String out;
            serializeJson(info, out);
            characteristic->setValue(out.c_str());
            characteristic->notify();
            Serial.printf("[BLE TX INFO]: %s\n", out.c_str());
            return;
        }

        // 3. Reset cấu hình Wi-Fi & MQTT
        if (strcmp(action, "reset_wifi") == 0) {
            clearStoredConfig();
            WiFi.disconnect(true);
            mqttClient.disconnect();
            notifyBleStatus("wifi_reset", "message", "config_cleared");
            return;
        }

        // 4. Lệnh điều khiển Relay tức thời qua BLE nếu cần
        if (strcmp(action, "set_power") == 0 || strcmp(action, "POWER_ON") == 0 || strcmp(action, "POWER_OFF") == 0) {
            bool pwr = false;
            if (doc.containsKey("power")) {
                pwr = doc["power"].as<bool>();
            } else {
                pwr = (strcmp(action, "POWER_ON") == 0);
            }
            setRelay(pwr);
            notifyBleStatus("power_updated", "relay", relayState ? "ON" : "OFF");
            return;
        }

        notifyBleStatus("error", "message", "unknown_action");
    }
};

void startBLE() {
    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[BLE] Khoi tao BLE Device [%s]...\n", BLE_DEVICE_NAME);
    Serial.printf("[BLE] Service Name: [%s] | UUID: %s\n", BLE_SERVICE_NAME, BLE_SERVICE_UUID);
    Serial.println("==================================================");

    BLEDevice::init(BLE_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new SmartSlugBLEServerCallbacks());

    // Khởi tạo Service với UUID chuẩn Syna
    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    // Characteristic giao tiếp 2 chiều (Read, Write, Notify)
    commCharacteristic = service->createCharacteristic(
        BLE_COMM_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    commCharacteristic->setCallbacks(new SmartSlugBLECharacteristicCallbacks());
    commCharacteristic->addDescriptor(new BLE2902());

    // Thêm User Description Descriptor (0x2901) mô tả tên service "Smart Slug"
    BLEDescriptor* userDesc = new BLEDescriptor((uint16_t)0x2901);
    userDesc->setValue(BLE_SERVICE_NAME);
    commCharacteristic->addDescriptor(userDesc);

    service->start();

    // Cấu hình gói quảng bá (Advertising)
    BLEAdvertising* adv = BLEDevice::getAdvertising();

    // Gói quảng bá chính (Gồm cờ Discoverable và Tên "Smart Slug")
    BLEAdvertisementData advData;
    advData.setName(BLE_DEVICE_NAME);
    advData.setFlags(0x06); // General Discoverable Mode

    // Gói phản hồi quét (Scan Response chứa Service UUID chuẩn Syna)
    BLEAdvertisementData scanData;
    scanData.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();

    Serial.printf("[BLE SAN SANG] Quang ba thanh cong! Ten thiet bi: '%s' | Service: '%s'\n", BLE_DEVICE_NAME, BLE_SERVICE_NAME);
}

// ====================================================================================================
// 9. KẾT NỐI WIFI VÀ TỰ ĐỘNG DÒ TÌM MQTT BROKER QUA MDNS
// ====================================================================================================
bool connectWiFi() {
    if (wifiSSID.length() == 0) {
        Serial.println("[WIFI] Chua co thong tin SSID. Cho nguoi dung Add Device qua BLE...");
        return false;
    }

    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[WIFI] BAT DAU KET NOI WIFI: %s ...\n", wifiSSID.c_str());
    Serial.println("==================================================");

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");

        wl_status_t st = WiFi.status();
        if (st == WL_CONNECT_FAILED) {
            Serial.println("\n[WIFI ERROR] Sai mat khau WiFi!");
            notifyBleStatus("wifi_failed", "error", "sai_mat_khau_wifi");
            return false;
        }
        if (st == WL_NO_SSID_AVAIL) {
            Serial.println("\n[WIFI ERROR] Khong tim thay ten mang WiFi (SSID)!");
            notifyBleStatus("wifi_failed", "error", "khong_tim_thay_ssid");
            return false;
        }

        if (millis() - start > 18000) {
            Serial.println("\n[WIFI ERROR] Ket noi WiFi timeout (Sai mat khau hoac song yeu)!");
            notifyBleStatus("wifi_failed", "error", "timeout_sai_mat_khau");
            return false;
        }
    }

    Serial.println();
    Serial.println("==================================================");
    Serial.println("[WIFI] DA KET NOI WIFI THANH CONG!");
    Serial.printf(" - Dia chi IP ESP32 : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf(" - Do manh tin hieu  : %d dBm\n", WiFi.RSSI());
    Serial.println("==================================================");

    saveStoredWiFi(wifiSSID, wifiPassword);
    notifyBleStatus("wifi_connected", "ip", WiFi.localIP().toString().c_str());

    // Tự động tìm kiếm MQTT Broker và kết nối
    discoverMQTTBroker();
    connectMQTT();

    return true;
}

bool discoverMQTTBroker() {
    Serial.println();
    Serial.println("[mDNS] Dang do tim Mosquitto Broker (_syna-mqtt._tcp)...");

    if (!MDNS.begin("smart-slug")) {
        Serial.println("[mDNS ERROR] Khong the khoi dong mDNS responder!");
    }

    int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);

    if (count > 0) {
        mqttBrokerIP = MDNS.address(0);
        mqttBrokerPort = MDNS.port(0);
        mqttBrokerFound = true;

        Serial.println("--------------------------------------------------");
        Serial.printf("[mDNS THANH CONG] Tim thay %d dich vu MQTT Broker!\n", count);
        Serial.printf(" -> Broker IP   : %s\n", mqttBrokerIP.toString().c_str());
        Serial.printf(" -> Broker Port : %d\n", mqttBrokerPort);
        Serial.println("--------------------------------------------------");

        saveStoredMQTT(mqttBrokerIP.toString(), mqttBrokerPort);
        notifyBleStatus("mdns_discovered", "broker", mqttBrokerIP.toString().c_str(), "port", String(mqttBrokerPort).c_str());
        return true;
    }

    // Fallback nếu không có mDNS
    if (mqttBrokerFound && mqttBrokerIP != IPAddress(0, 0, 0, 0)) {
        Serial.printf("[mDNS CẢNH BÁO] Dung MQTT Broker da luu trong Flash: %s:%d\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);
        return true;
    }

    Serial.println("[mDNS CẢNH BÁO] Khong tim thay MQTT qua mDNS. Dung Fallback IP:");
    mqttBrokerIP.fromString(FALLBACK_MQTT_IP);
    mqttBrokerPort = DEFAULT_MQTT_PORT;
    mqttBrokerFound = true;
    Serial.printf(" -> Fallback Broker IP: %s:%d\n", FALLBACK_MQTT_IP, DEFAULT_MQTT_PORT);
    return true;
}

// ====================================================================================================
// 10. KẾT NỐI VÀ XỬ LÝ MQTT (PUBLISH & SUBSCRIBE)
// ====================================================================================================
void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!mqttBrokerFound || mqttBrokerIP == INADDR_NONE) {
        discoverMQTTBroker();
    }

    mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(2048);
    mqttClient.setSocketTimeout(1);

    Serial.println();
    Serial.printf("[MQTT] Dang ket noi toi Broker: %s:%d ...\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);

    String clientId = "SmartSlug-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    // LWT (Last Will and Testament) topic
    String lwtTopic = "hesta/smart-slug/status";
    if (currentNodeId.length() > 0) {
        lwtTopic = "hesta/nodes/" + currentNodeId + "/status";
    }
    const char* lwtPayload = "OFFLINE";

    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
        connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, lwtTopic.c_str(), 1, true, lwtPayload);
    } else {
        connected = mqttClient.connect(clientId.c_str(), lwtTopic.c_str(), 1, true, lwtPayload);
    }

    if (connected) {
        Serial.println("==================================================");
        Serial.println("[MQTT] KET NOI BROKER THANH CONG!");
        Serial.printf(" - Client ID   : %s\n", clientId.c_str());
        Serial.printf(" - Product     : %s\n", PRODUCT_NAME);
        Serial.println("==================================================");

        // Báo kết nối thành công về App qua BLE
        notifyBleStatus("mqtt_connected");
        notifyBleStatus("ready");

        // Báo trạng thái Online lên MQTT
        mqttClient.publish(lwtTopic.c_str(), "ONLINE", true);
        publishRelayStatus("ONLINE");

        // 1. Đăng ký các Topic nếu đã có Node ID / Device ID
        if (currentNodeId.length() > 0) {
            if (currentDeviceId.length() > 0) {
                String subDevCmd = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/command";
                mqttClient.subscribe(subDevCmd.c_str(), 1);
                Serial.printf("[MQTT SUB] %s\n", subDevCmd.c_str());
            }
            String subNodeDevWildcard = "hesta/nodes/" + currentNodeId + "/devices/+/command";
            mqttClient.subscribe(subNodeDevWildcard.c_str(), 1);
            Serial.printf("[MQTT SUB] %s\n", subNodeDevWildcard.c_str());

            String subNodeCmd = "hesta/nodes/" + currentNodeId + "/command";
            mqttClient.subscribe(subNodeCmd.c_str(), 1);
            Serial.printf("[MQTT SUB] %s\n", subNodeCmd.c_str());
        }

        // 2. Đăng ký các Topic wildcard toàn cục & topic riêng của dòng Smart-Slug
        // (Đảm bảo điều khiển được ngay cả khi Node ID / Device ID đang để trống "")
        mqttClient.subscribe("hesta/nodes/+/devices/+/command", 1);
        mqttClient.subscribe("hesta/nodes/+/command", 1);
        mqttClient.subscribe("hesta/smart-slug/command", 1);
        mqttClient.subscribe("hesta/devices/smart-slug/command", 1);

        Serial.println("[MQTT SUB] hesta/nodes/+/devices/+/command (Wildcard san sang)");
        Serial.println("[MQTT SUB] hesta/smart-slug/command");

        // Gửi trạng thái State & Telemetry ban đầu
        publishRelayState();
        publishTelemetry();
    } else {
        Serial.printf("[MQTT ERROR] Ket noi that bai, rc=%d. Se thu lai sau...\n", mqttClient.state());
        notifyBleStatus("mqtt_failed", "rc", String(mqttClient.state()).c_str());
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char payloadStr[length + 1];
    memcpy(payloadStr, payload, length);
    payloadStr[length] = '\0';

    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[MQTT NHAN LENH] Topic: %s\n", topic);
    Serial.printf("[MQTT PAYLOAD]  : %s\n", payloadStr);
    Serial.println("==================================================");

    // Trích xuất deviceId từ topic nếu có định dạng .../devices/{deviceId}/command
    String targetDeviceId = "";
    String tStr = String(topic);
    int devIdx = tStr.indexOf("/devices/");
    if (devIdx >= 0) {
        int nextSlash = tStr.indexOf("/", devIdx + 9);
        if (nextSlash > devIdx + 9) {
            targetDeviceId = tStr.substring(devIdx + 9, nextSlash);
        }
    }

    processCommandPayload(targetDeviceId, payloadStr);
}

void processCommandPayload(const String& topicDeviceId, const char* payloadStr) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    DeserializationError error = deserializeJson(doc, payloadStr);
    if (error) {
        Serial.printf("[JSON ERROR] Khong the parse JSON: %s\n", error.c_str());
        return;
    }

    // Trích xuất Device ID mục tiêu
    String targetDevId = topicDeviceId;
    if (targetDevId.length() == 0) {
        if (doc.containsKey("deviceId")) {
            targetDevId = (const char*)doc["deviceId"];
        } else if (doc.containsKey("target")) {
            targetDevId = (const char*)doc["target"];
        }
    }

    // Nếu thiết bị đã có Device ID cụ thể và gói tin chỉ định Device ID khác -> Bỏ qua
    if (currentDeviceId.length() > 0 && targetDevId.length() > 0 && targetDevId != currentDeviceId && targetDevId != "+" && targetDevId != "*") {
        Serial.printf("[MQTT IGNORE] Lenh danh cho deviceId '%s', khong phai cua thiet bi nay ('%s').\n",
                      targetDevId.c_str(), currentDeviceId.c_str());
        return;
    }

    const char* commandId = doc["commandId"] | "";
    String action = doc["action"] | "";
    action.toUpperCase();

    // Lấy tham số từ root hoặc nested parameters
    JsonObject params;
    if (doc["parameters"].is<JsonObject>()) {
        params = doc["parameters"].as<JsonObject>();
    }

    bool hasPowerParam = false;
    bool powerValue = false;

    if (doc.containsKey("power")) {
        hasPowerParam = true;
        if (doc["power"].is<bool>()) {
            powerValue = doc["power"].as<bool>();
        } else {
            String pStr = doc["power"].as<String>();
            pStr.toUpperCase();
            powerValue = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
        }
    } else if (!params.isNull() && params.containsKey("power")) {
        hasPowerParam = true;
        if (params["power"].is<bool>()) {
            powerValue = params["power"].as<bool>();
        } else {
            String pStr = params["power"].as<String>();
            pStr.toUpperCase();
            powerValue = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
        }
    }

    // Kiểm tra thêm tham số state ("ON" / "OFF")
    if (!hasPowerParam) {
        if (doc.containsKey("state")) {
            String sStr = doc["state"].as<String>();
            sStr.toUpperCase();
            hasPowerParam = true;
            powerValue = (sStr == "ON" || sStr == "TRUE" || sStr == "1");
        } else if (!params.isNull() && params.containsKey("state")) {
            String sStr = params["state"].as<String>();
            sStr.toUpperCase();
            hasPowerParam = true;
            powerValue = (sStr == "ON" || sStr == "TRUE" || sStr == "1");
        }
    }

    // Thực thi các hành động
    if (action == "POWER_ON" || action == "TURN_ON" || action == "ON") {
        setRelay(true);
        publishRelayAck(commandId, "SUCCESS");
    } else if (action == "POWER_OFF" || action == "TURN_OFF" || action == "OFF") {
        setRelay(false);
        publishRelayAck(commandId, "SUCCESS");
    } else if (action == "TOGGLE") {
        toggleRelay();
        publishRelayAck(commandId, "SUCCESS");
    } else if (action == "SET_POWER" || action == "SET_STATE" || hasPowerParam) {
        setRelay(powerValue);
        publishRelayAck(commandId, "SUCCESS");
    } else if (action == "GET_STATE" || action == "STATUS") {
        publishRelayState();
        publishRelayAck(commandId, "SUCCESS");
    } else {
        Serial.printf("[MQTT WARN] Action chua duoc ho tro hoac khong ro: %s\n", action.c_str());
        publishRelayAck(commandId, "UNKNOWN_ACTION");
    }
}

// ====================================================================================================
// 11. MQTT PUBLISHERS (STATE, ACK, TELEMETRY, STATUS)
// ====================================================================================================
void publishRelayState() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["nodeId"]     = currentNodeId;
    doc["deviceId"]   = currentDeviceId;
    doc["product"]    = PRODUCT_NAME;
    doc["deviceType"] = DEVICE_TYPE;
    doc["power"]      = relayState;
    doc["state"]      = relayState ? "ON" : "OFF";

    JsonObject stateObj = doc["stateObj"].to<JsonObject>();
    stateObj["power"] = relayState;
    stateObj["relay"] = relayState ? "ON" : "OFF";

    String payload;
    serializeJson(doc, payload);

    // Gửi lên Topic cụ thể nếu có Device ID
    if (currentNodeId.length() > 0 && currentDeviceId.length() > 0) {
        String stateTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/state";
        mqttClient.publish(stateTopic.c_str(), payload.c_str(), true);
    }
    if (currentNodeId.length() > 0) {
        String nodeStateTopic = "hesta/nodes/" + currentNodeId + "/state";
        mqttClient.publish(nodeStateTopic.c_str(), payload.c_str(), true);
    }

    // Luôn gửi lên topic chung của sản phẩm Smart-Slug
    mqttClient.publish("hesta/smart-slug/state", payload.c_str(), true);

    Serial.printf("[MQTT PUB STATE] -> %s\n", payload.c_str());
}

void publishRelayAck(const char* commandId, const char* statusMsg) {
    if (!mqttClient.connected() || commandId == nullptr || strlen(commandId) == 0) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["commandId"]  = commandId;
    doc["nodeId"]     = currentNodeId;
    doc["deviceId"]   = currentDeviceId;
    doc["product"]    = PRODUCT_NAME;
    doc["status"]     = statusMsg;
    doc["power"]      = relayState;

    String payload;
    serializeJson(doc, payload);

    if (currentNodeId.length() > 0 && currentDeviceId.length() > 0) {
        String ackTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/ack";
        mqttClient.publish(ackTopic.c_str(), payload.c_str());
    }
    mqttClient.publish("hesta/smart-slug/ack", payload.c_str());

    Serial.printf("[MQTT PUB ACK] -> %s\n", payload.c_str());
}

void publishTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["nodeId"]     = currentNodeId;
    doc["deviceId"]   = currentDeviceId;
    doc["product"]    = PRODUCT_NAME;
    doc["deviceType"] = DEVICE_TYPE;
    doc["power"]      = relayState;
    doc["wifi_rssi"]  = WiFi.RSSI();
    doc["ip"]         = WiFi.localIP().toString();
    doc["uptime_s"]   = millis() / 1000;

    String payload;
    serializeJson(doc, payload);

    if (currentNodeId.length() > 0 && currentDeviceId.length() > 0) {
        String telemTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/telemetry";
        mqttClient.publish(telemTopic.c_str(), payload.c_str());
    }
    mqttClient.publish("hesta/smart-slug/telemetry", payload.c_str());

    Serial.printf("[MQTT PUB TELEMETRY] -> %s\n", payload.c_str());
}

void publishRelayStatus(const char* status) {
    if (!mqttClient.connected()) return;

    if (currentNodeId.length() > 0 && currentDeviceId.length() > 0) {
        String statusTopic = "hesta/nodes/" + currentNodeId + "/devices/" + currentDeviceId + "/status";
        mqttClient.publish(statusTopic.c_str(), status, true);
    }
    mqttClient.publish("hesta/smart-slug/status", status, true);
}

// ====================================================================================================
// 12. XỬ LÝ NÚT NHẤN VẬT LÝ (PHYSICAL BUTTON)
// ====================================================================================================
void handlePhysicalButton() {
#if ENABLE_PHYSICAL_BUTTON
    // Chân GPIO 0 có điện trở kéo lên, khi nhấn nút mức logic là LOW
    bool reading = (digitalRead(BUTTON_PIN) == LOW);

    if (reading && !buttonIsPressed) {
        // Bắt đầu nhấn nút
        buttonIsPressed = true;
        buttonPressStartTime = millis();
        longPressTriggered = false;
    } else if (reading && buttonIsPressed) {
        // Đang giữ nút
        if (!longPressTriggered && (millis() - buttonPressStartTime >= BUTTON_LONG_PRESS_MS)) {
            longPressTriggered = true;
            Serial.println();
            Serial.println("==================================================");
            Serial.println("[BUTTON] DA NHAN GIU 5S -> RESET CAU HINH XUAT XUONG!");
            Serial.println("==================================================");
            clearStoredConfig();
            WiFi.disconnect(true);
            mqttClient.disconnect();
            notifyBleStatus("wifi_reset", "message", "factory_reset_by_button");

            // Nháy relay 3 lần để báo hiệu reset thành công
            for (int i = 0; i < 3; i++) {
                digitalWrite(RELAY_PIN, RELAY_ON);
                delay(150);
                digitalWrite(RELAY_PIN, RELAY_OFF);
                delay(150);
            }
        }
    } else if (!reading && buttonIsPressed) {
        // Nhả nút
        unsigned long pressDuration = millis() - buttonPressStartTime;
        buttonIsPressed = false;

        if (!longPressTriggered && pressDuration >= BUTTON_DEBOUNCE_MS) {
            Serial.println("[BUTTON] Nhan nha -> Dao trang thai Relay");
            toggleRelay();
        }
    }
#endif
}

// ====================================================================================================
// 13. GIAO DIỆN DÒNG LỆNH SERIAL MONITOR (TEST NHANH BẰNG BÀN PHÍM)
// ====================================================================================================
void handleSerialCommands() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    cmd.toLowerCase();

    if (cmd == "on" || cmd == "1") {
        setRelay(true);
    } else if (cmd == "off" || cmd == "0") {
        setRelay(false);
    } else if (cmd == "toggle" || cmd == "t") {
        toggleRelay();
    } else if (cmd == "status" || cmd == "s") {
        printSystemStatus();
    } else if (cmd == "reset") {
        clearStoredConfig();
        WiFi.disconnect(true);
        mqttClient.disconnect();
        Serial.println("[SERIAL] Da xoa cau hinh WiFi/MQTT. Vui long khoi dong lai ESP32 hoac provision lai qua BLE.");
    } else if (cmd == "wifi") {
        connectWiFi();
    } else if (cmd == "help" || cmd == "?") {
        printHelp();
    } else {
        Serial.printf("[SERIAL] Lenh khong hop le: '%s'. Go 'help' de xem danh sach lenh.\n", cmd.c_str());
    }
}

void printHelp() {
    Serial.println();
    Serial.println("=================== HUONG DAN LENH SERIAL ===================");
    Serial.println(" on      hoac 1      : Bat Relay");
    Serial.println(" off     hoac 0      : Tat Relay");
    Serial.println(" toggle  hoac t      : Dao trang thai Relay");
    Serial.println(" status  hoac s      : In thong tin trang thai Relay, WiFi, MQTT");
    Serial.println(" reset               : Xoa toan bo cau hinh WiFi & MQTT trong Flash");
    Serial.println(" wifi                : Thu ket noi lai WiFi theo thong tin da luu");
    Serial.println(" help    hoac ?      : In menu tro giup nay");
    Serial.println("=============================================================");
}

void printSystemStatus() {
    Serial.println();
    Serial.println("=================== TRANG THAI HE THONG ===================");
    Serial.printf(" - San pham       : %s\n", PRODUCT_NAME);
    Serial.printf(" - BLE Name       : %s\n", BLE_DEVICE_NAME);
    Serial.printf(" - BLE Service    : %s\n", BLE_SERVICE_NAME);
    Serial.printf(" - Node ID        : %s\n", currentNodeId.length() > 0 ? currentNodeId.c_str() : "(DE TRONG)");
    Serial.printf(" - Device ID      : %s\n", currentDeviceId.length() > 0 ? currentDeviceId.c_str() : "(DE TRONG)");
    Serial.printf(" - Relay (GPIO %d): %s\n", RELAY_PIN, relayState ? "BAT (ON)" : "TAT (OFF)");
    Serial.printf(" - WiFi Status    : %s (SSID: %s | IP: %s | RSSI: %d dBm)\n",
                  (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
                  wifiSSID.c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    Serial.printf(" - MQTT Status    : %s (Broker: %s:%d)\n",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  mqttBrokerIP.toString().c_str(),
                  mqttBrokerPort);
    Serial.printf(" - BLE Client     : %s\n", bleClientConnected ? "CONNECTED" : "WAITING / ADVERTISING");
    Serial.println("===========================================================");
}

// ====================================================================================================
// 14. SETUP & LOOP CHÍNH
// ====================================================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("====================================================================");
    Serial.printf("   KHOI DONG %s (BLE SERVICE: %s)\n", PRODUCT_NAME, BLE_SERVICE_NAME);
    Serial.println("====================================================================");

    // Cấu hình chân Relay
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    relayState = false;

    // Cấu hình chân nút nhấn vật lý
#if ENABLE_PHYSICAL_BUTTON
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.printf("[BUTTON] Nut nhan vat ly duoc kich hoat tren GPIO %d\n", BUTTON_PIN);
#endif

    // Nạp cấu hình đã lưu trong Flash NVS (nếu có)
    loadStoredConfig();

    // Bắt đầu dịch vụ BLE Provisioning với tên Service "Smart Slug"
    startBLE();

    // Nếu đã lưu WiFi trước đó trong Flash NVS thì thử tự động kết nối
    if (wifiSSID.length() > 0) {
        Serial.println("[AUTO-CONNECT] Tim thay WiFi trong Flash NVS, tien hanh ket noi...");
        connectWiFi();
    } else {
        Serial.println("[PROVISIONING] Chua co WiFi trong Flash. Vui long mo App va Add Device qua BLE...");
    }

    printHelp();
}

void loop() {
    // 1. Xử lý yêu cầu kết nối Wi-Fi từ sự kiện BLE Provisioning
    if (shouldConnectWiFi) {
        shouldConnectWiFi = false;
        connectWiFi();
    }

    // 2. Tự phục hồi kết nối Wi-Fi nếu bị rớt mạng
    if (WiFi.status() != WL_CONNECTED && wifiSSID.length() > 0) {
        unsigned long now = millis();
        if (now - lastWiFiReconnectAttempt > 10000) {
            lastWiFiReconnectAttempt = now;
            Serial.println("[WIFI] Mat ket noi WiFi! Dang thu ket noi lai...");
            WiFi.disconnect();
            WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
        }
    }

    // 3. Duy trì kết nối MQTT và xử lý gói tin đến
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            unsigned long now = millis();
            if (now - lastMqttReconnectAttempt > 5000) {
                lastMqttReconnectAttempt = now;
                Serial.println("[MQTT] Mat ket noi MQTT! Dang thu ket noi lai...");
                connectMQTT();
            }
        } else {
            mqttClient.loop();

            // Gửi Telemetry định kỳ
            unsigned long now = millis();
            if (now - lastTelemetryPublish > TELEMETRY_INTERVAL) {
                lastTelemetryPublish = now;
                publishTelemetry();
            }
        }
    }

    // 4. Xử lý nút bấm vật lý tại chỗ
    handlePhysicalButton();

    // 5. Xử lý lệnh gõ qua Serial Monitor
    handleSerialCommands();

    delay(10);
}
