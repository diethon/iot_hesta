#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
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
// HESTA / SYNA SMART HOME - COMBO CONTROLLER: DAIKIN AC & TCL TV
// 
// Tính năng:
// 1. Điều khiển Máy lạnh Daikin qua giao thức DAIKIN64 (64-bit).
// 2. Tích hợp điều khiển TV TCL (Nikai 24-bit protocol) từ Send_Frame_TV:
//    - Đầy đủ 24 nút: POWER, MUTE, INPUT, VOL_UP, VOL_DOWN, CH_UP, CH_DOWN,
//      UP, DOWN, LEFT, RIGHT, OK, BACK, HOME, MENU, và các số 0-9.
//    - Hỗ trợ lệnh nút trực tiếp, action chuẩn (POWER_ON, VOL_UP, MUTE...),
//      lệnh SEND_BUTTON, và phát mã xung thô SEND_IR_CODE.
// 3. Sử dụng chung chân phát IR LED (chân 4 qua transistor 2N2222).
// 4. BLE WiFi Provisioning theo chuẩn Hesta (Service UUID ...9000, Char ...9001).
// 5. Tự động lưu cấu hình WiFi, MQTT, AC State và TV State vào Flash NVS (Preferences).
// 6. mDNS Auto-Discovery: tự động tìm kiếm Mosquitto Broker (_syna-mqtt._tcp).
// 7. Kết nối MQTT với 2 thiết bị độc lập trên cùng 1 mạch ESP32:
//    - AC Device ID: 05d8a4fd-d7f1-4c62-a70e-a4213552b054 (AIR_CONDITIONER)
//    - TV Device ID: 7a9e5b3c-6d2f-4e8a-b1c4-9f8e7d6c5b4a (TV)
//    - Nhận lệnh thông minh qua Device ID riêng hoặc qua cờ target: "tv" / "ac".
// 8. Tích hợp Web Server trực quan (Port 80) cho phép điều khiển TV & AC
//    trực tiếp trên trình duyệt bằng điện thoại / máy tính qua địa chỉ IP của ESP32.
// 9. Gửi State, ACK (kèm commandId), Status (ONLINE/OFFLINE) và Telemetry định kỳ.
// 10. Giữ nguyên giao tiếp điều khiển qua Serial Monitor (115200 baud).
// ============================================================

// ============================================================
// HARDWARE PIN CONFIGURATION
// ============================================================
// Chân phát hồng ngoại IR LED (qua transistor 2N2222 kích dòng)
// Dùng chung cho cả AC Daikin và TV TCL
#define IR_LED_PIN 4

// ============================================================
// BACKEND HESTA - NODE & DEVICE IDENTIFIERS
// ============================================================
#define NODE_ID          "32442d7f-dfcf-4b2f-a380-adba32876cec"
#define DEFAULT_NODE_ID  NODE_ID

// 1. Máy lạnh Daikin
#define AC_DEVICE_ID     "05d8a4fd-d7f1-4c62-a70e-a4213552b054"
#define AC_DEVICE_TYPE   "AIR_CONDITIONER"

// 2. Ti vi TCL (Send_Frame_TV)
#define TV_DEVICE_ID     "7a9e5b3c-6d2f-4e8a-b1c4-9f8e7d6c5b4a"
#define TV_DEVICE_TYPE   "TV"

// Giữ tương thích ngược với code cũ:
#define DEVICE_ID        AC_DEVICE_ID
#define DEVICE_TYPE      AC_DEVICE_TYPE

// ============================================================
// CHẾ ĐỘ TEST OFFLINE (SERIAL TEST MODE) & CẤU HÌNH HỆ THỐNG
// ============================================================
#define OFFLINE_TEST_MODE false
bool offlineTestMode = OFFLINE_TEST_MODE;

// ============================================================
// CẤU HÌNH WIFI & MQTT BROKER FALLBACK (CHUẨN SYNA / HESTA)
// ============================================================
#define DEFAULT_WIFI_SSID     "NGUYEN-TOAN"
#define DEFAULT_WIFI_PASSWORD "12345678"

#define FALLBACK_MQTT_IP      "192.168.137.107"
#define DEFAULT_MQTT_PORT     1883
#define MQTT_USER             ""
#define MQTT_PASSWORD         ""

#define MQTT_DISCOVERY_SERVICE  "syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "tcp"

#define FIRMWARE_VERSION        "2.3.0"
#define DEVICE_HUB_NAME         "SYNA-IR-HUB"

// Topics MQTT chuyên biệt
const String TOPIC_DEVICES_COMMAND    = "hesta/nodes/" NODE_ID "/devices/+/command";
const String TOPIC_NODE_COMMAND       = "hesta/nodes/" NODE_ID "/command";

// 1. Máy lạnh Daikin
const String TOPIC_AC_COMMAND         = "hesta/nodes/" NODE_ID "/devices/" AC_DEVICE_ID "/command";
const String TOPIC_AC_STATE           = "hesta/nodes/" NODE_ID "/devices/" AC_DEVICE_ID "/state";
const String TOPIC_AC_ACK             = "hesta/nodes/" NODE_ID "/devices/" AC_DEVICE_ID "/ack";
const String TOPIC_AC_TELEMETRY       = "hesta/nodes/" NODE_ID "/devices/" AC_DEVICE_ID "/telemetry";
const String TOPIC_AC_STATUS          = "hesta/nodes/" NODE_ID "/devices/" AC_DEVICE_ID "/status";

// 2. Ti vi TCL
const String TOPIC_TV_COMMAND         = "hesta/nodes/" NODE_ID "/devices/" TV_DEVICE_ID "/command";
const String TOPIC_TV_STATE           = "hesta/nodes/" NODE_ID "/devices/" TV_DEVICE_ID "/state";
const String TOPIC_TV_ACK             = "hesta/nodes/" NODE_ID "/devices/" TV_DEVICE_ID "/ack";
const String TOPIC_TV_TELEMETRY       = "hesta/nodes/" NODE_ID "/devices/" TV_DEVICE_ID "/telemetry";
const String TOPIC_TV_STATUS          = "hesta/nodes/" NODE_ID "/devices/" TV_DEVICE_ID "/status";

// ============================================================
// BLE PROVISIONING CONFIGURATION (SYNA MOBILE APP)
// ============================================================
#define BLE_DEVICE_NAME        DEVICE_HUB_NAME
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID     "12345678-1234-1234-1234-123456789001"

// ============================================================
// TV BUTTONS DEFINITION (TCL - NIKAI 24-BIT IR PROTOCOL)
// ============================================================
struct TvBtn {
  const char* name;
  uint32_t code;
};

const TvBtn TV_BUTTONS[] = {
  {"POWER",    0x0D5F2A}, {"MUTE",     0x0C0F3F}, {"INPUT",    0x05CFA3},
  {"VOL_UP",   0x0D0F2F}, {"VOL_DOWN", 0x0D1F2E},
  {"CH_UP",    0x0D2F2D}, {"CH_DOWN",  0x0D3F2C},
  {"UP",       0x0A6F59}, {"DOWN",     0x0A7F58},
  {"LEFT",     0x0A9F56}, {"RIGHT",    0x0A8F57}, {"OK",       0x00BFF4},
  {"BACK",     0x0D8F27}, {"HOME",     0x0F7F08}, {"MENU",     0x013FEC},
  {"0",        0x0CFF30}, {"1",        0x0CEF31}, {"2",        0x0CDF32}, {"3", 0x0CCF33},
  {"4",        0x0CBF34}, {"5",        0x0CAF35}, {"6",        0x0C9F36},
  {"7",        0x0C8F37}, {"8",        0x0C7F38}, {"9",        0x0C6F39}
};
const int N_TV_BUTTONS = sizeof(TV_BUTTONS) / sizeof(TV_BUTTONS[0]);

// ============================================================
// GLOBAL OBJECTS & DRIVERS
// ============================================================
IRDaikin64 ac(IR_LED_PIN);
IRsend irsend(IR_LED_PIN);

Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer webServer(80);

BLEServer* bleServer = nullptr;
BLECharacteristic* commCharacteristic = nullptr;

bool bleClientConnected = false;
volatile bool shouldConnectWiFi = false;

String wifiSSID = DEFAULT_WIFI_SSID;
String wifiPassword = DEFAULT_WIFI_PASSWORD;

bool mqttBrokerFound = false;
IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = DEFAULT_MQTT_PORT;

// Dynamic Node ID để tự thích ứng với Node ID gửi từ Backend
String currentNodeId = NODE_ID;

// Virtual State của Điều hòa (AC)
bool acPower = false;
uint8_t currentTemp = 26;
uint8_t currentMode = kDaikin64Cool;
uint8_t currentFan  = kDaikin64FanAuto;
bool currentSwing   = false;

// Virtual State của TV (TCL)
bool tvPower = false;
bool tvMute = false;
uint8_t tvVolume = 20;
uint16_t tvChannel = 1;
String tvLastButton = "NONE";

// Timers
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastDiscoveryAttempt = 0;
unsigned long lastTelemetryPublish = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 30000;

// Gói raw mẫu đã bắt được từ remote gốc Daikin (Cool, 26C, Fan Low, kèm Power Toggle)
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
void loadNetworkConfigFromNVS();
void saveMQTTConfigToNVS(const String& ipStr, uint16_t port);
void saveWiFiConfigToNVS(const String& ssid, const String& pass);
void clearStoredWiFi();
void loadStoredWiFi();
void saveStoredWiFi(const String& ssid, const String& password);
void loadStoredMQTT();
void saveStoredMQTT(const String& host, uint16_t port);
void loadStoredACState();
void saveStoredACState();
void loadStoredTVState();
void saveStoredTVState();

void startBLE();
void notifyBLE(const String& message);
void notifyBleStatus(const char* status, const char* key1 = nullptr, const char* val1 = nullptr, const char* key2 = nullptr, const char* val2 = nullptr);

bool connectWiFi();
void handleWiFiProvisioning();
bool discoverMQTTBroker();
bool discoverMQTT();
bool connectMQTT();
void publishNodeDiscovery();

void handleWiFiProvisioning();
bool discoverMQTT();
bool connectMQTT();

void sendIRState(bool toggle);
void sendRawSignal();
bool sendTvButtonByName(String name);
bool sendTvCode(uint32_t code);

void publishACState(const char* targetDevId = nullptr);
void publishACACK(const char* commandId, const char* targetDevId = nullptr);
void publishTVState(const char* targetDevId = nullptr);
void publishTVACK(const char* commandId, const char* targetDevId = nullptr, const char* status = "SUCCESS");
void publishStatus(const char* status);
void publishTVStatus(const char* status);
void publishTelemetry();

void processCommand(const char* action, JsonObject& params, const char* commandId = nullptr, const char* reqDeviceId = nullptr);
void processTvCommand(String act, JsonObject& doc, const char* commandId = nullptr, const char* reqDeviceId = nullptr);
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setupWebServer();
void handleRoot();
void handleWebSend();
void handleWebAc();
void handleWebStatus();

void handleSerial();
void printStatus();

const char* modeToString(uint8_t mode);
uint8_t stringToMode(String str);
const char* fanToString(uint8_t fan);
uint8_t stringToFan(String str);

// ============================================================
// HELPER CONVERSIONS (AC)
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
// TV IR TRANSMITTER LOGIC (NIKAI 24-BIT)
// ============================================================
bool sendTvButtonByName(String name) {
  name.trim();
  name.toUpperCase();

  // Chuẩn hóa alias thường gặp từ API/App
  if (name == "VOL+" || name == "VOLUME_UP") name = "VOL_UP";
  if (name == "VOL-" || name == "VOLUME_DOWN") name = "VOL_DOWN";
  if (name == "CH+" || name == "CHANNEL_UP") name = "CH_UP";
  if (name == "CH-" || name == "CHANNEL_DOWN") name = "CH_DOWN";
  if (name == "ENTER" || name == "SELECT") name = "OK";
  if (name == "RETURN") name = "BACK";
  if (name == "SOURCE") name = "INPUT";

  for (int i = 0; i < N_TV_BUTTONS; i++) {
    if (name == TV_BUTTONS[i].name) {
      irsend.sendNikai(TV_BUTTONS[i].code, 24, 1);
      Serial.printf("[IR TV] Đã phát sóng Nikai nút %s (0x%06X)\n", TV_BUTTONS[i].name, TV_BUTTONS[i].code);
      return true;
    }
  }
  Serial.printf("[IR TV] Không tìm thấy mã nút: %s\n", name.c_str());
  return false;
}

bool sendTvCode(uint32_t code) {
  irsend.sendNikai(code, 24, 1);
  Serial.printf("[IR TV] Đã phát sóng mã Nikai trực tiếp: 0x%06X\n", code);
  return true;
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
  ac.setPowerToggle(false); // Ngay lập tức đặt lại false để các lệnh sau không vô tình đảo nguồn

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
  acPower = !acPower; // Gói raw có sẵn bit toggle
  Serial.println("[IR RAW] Đã phát gói xung raw 137 xung 38kHz (Đã đảo trạng thái nguồn).");
  saveStoredACState();
  publishACState();
}

// ============================================================
// NVS STORAGE (PREFERENCES - CHUẨN SYNA_NET & SYNA_DEV)
// ============================================================
void loadNetworkConfigFromNVS() {
  preferences.begin("syna_net", true);
  wifiSSID = preferences.getString("ssid", DEFAULT_WIFI_SSID);
  wifiPassword = preferences.getString("pass", DEFAULT_WIFI_PASSWORD);
  String storedMqtt = preferences.getString("mqtt_ip", "");
  uint16_t storedPort = preferences.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
  preferences.end();

  // Tương thích ngược nếu trước đó đã lưu ở namespace cũ "wifi" / "mqtt"
  if (wifiSSID == DEFAULT_WIFI_SSID) {
    preferences.begin("wifi", true);
    String oldSsid = preferences.getString("ssid", "");
    String oldPass = preferences.getString("password", "");
    preferences.end();
    if (oldSsid.length() > 0) {
      wifiSSID = oldSsid;
      wifiPassword = oldPass;
    }
  }

  if (storedMqtt.length() == 0) {
    preferences.begin("mqtt", true);
    String oldHost = preferences.getString("host", "");
    uint16_t oldPort = preferences.getUShort("port", 0);
    preferences.end();
    if (oldHost.length() > 0) {
      storedMqtt = oldHost;
      storedPort = oldPort;
    }
  }

  if (storedMqtt.length() > 0) {
    mqttBrokerIP.fromString(storedMqtt);
    mqttBrokerPort = storedPort;
    mqttBrokerFound = true;
    Serial.printf("[NVS] Da tim thay MQTT Broker luu trong Flash: %s:%d\n", storedMqtt.c_str(), storedPort);
  } else {
    Serial.printf("[NVS] WiFi: %s | MQTT chua co san, se do mDNS\n", wifiSSID.c_str());
  }
}

void saveMQTTConfigToNVS(const String& ipStr, uint16_t port) {
  preferences.begin("syna_net", false);
  preferences.putString("mqtt_ip", ipStr);
  preferences.putUShort("mqtt_port", port);
  preferences.end();
}

void saveWiFiConfigToNVS(const String& ssid, const String& pass) {
  preferences.begin("syna_net", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
}

void clearStoredWiFi() {
  preferences.begin("syna_net", false);
  preferences.clear();
  preferences.end();

  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  preferences.begin("mqtt", false);
  preferences.clear();
  preferences.end();

  mqttBrokerFound = false;
  Serial.println("[NVS] Da xoa toan bo cau hinh mang trong flash NVS.");
}

// Aliases tương thích ngược
void loadStoredWiFi() { loadNetworkConfigFromNVS(); }
void saveStoredWiFi(const String& ssid, const String& password) { saveWiFiConfigToNVS(ssid, password); }
void loadStoredMQTT() { /* da nạp trong loadNetworkConfigFromNVS */ }
void saveStoredMQTT(const String& host, uint16_t port) { saveMQTTConfigToNVS(host, port); }

void loadStoredACState() {
  preferences.begin("ac_state", true);
  acPower      = preferences.getBool("power", false);
  currentTemp  = preferences.getUChar("temp", 26);
  currentMode  = preferences.getUChar("mode", kDaikin64Cool);
  currentFan   = preferences.getUChar("fan", kDaikin64FanAuto);
  currentSwing = preferences.getBool("swing", false);
  preferences.end();

  if (currentTemp < 16 || currentTemp > 30) currentTemp = 26;
  Serial.println("[NVS] Đã khôi phục trạng thái Điều hòa từ flash.");
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

void loadStoredTVState() {
  preferences.begin("tv_state", true);
  tvPower     = preferences.getBool("power", false);
  tvMute      = preferences.getBool("mute", false);
  tvVolume    = preferences.getUChar("vol", 20);
  tvChannel   = preferences.getUShort("ch", 1);
  tvLastButton = preferences.getString("btn", "NONE");
  preferences.end();
  Serial.println("[NVS] Đã khôi phục trạng thái TV từ flash.");
}

void saveStoredTVState() {
  preferences.begin("tv_state", false);
  preferences.putBool("power", tvPower);
  preferences.putBool("mute", tvMute);
  preferences.putUChar("vol", tvVolume);
  preferences.putUShort("ch", tvChannel);
  preferences.putString("btn", tvLastButton);
  preferences.end();
}

// ============================================================
// BLE PROVISIONING (CHUẨN HỆ SINH THÁI SYNA MOBILE APP)
// ============================================================
void notifyBleStatus(const char* status, const char* key1, const char* val1, const char* key2, const char* val2) {
  if (!bleClientConnected || commCharacteristic == nullptr) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(512);
#endif

  doc["status"] = status;
  doc["nodeId"] = currentNodeId;
  if (key1 && val1) doc[key1] = val1;
  if (key2 && val2) doc[key2] = val2;

  String jsonStr;
  serializeJson(doc, jsonStr);

  commCharacteristic->setValue(jsonStr.c_str());
  commCharacteristic->notify();
  delay(40);
  Serial.printf("[BLE NOTIFY -> APP]: %s\n", jsonStr.c_str());
}

void notifyBLE(const String& message) {
  if (commCharacteristic != nullptr && bleClientConnected) {
    commCharacteristic->setValue(message.c_str());
    commCharacteristic->notify();
    Serial.printf("[BLE GỬI APP]: %s\n", message.c_str());
  }
}

class SynaBLEServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleClientConnected = true;
    Serial.println("\n[BLE] APP DI DONG DA KET NOI BLE!");
    notifyBleStatus("ble_connected", "device", DEVICE_HUB_NAME);
  }

  void onDisconnect(BLEServer* server) override {
    bleClientConnected = false;
    Serial.println("\n[BLE] APP NGAT KET NOI. Bat lai che do quang ba (Advertising)...");
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
      notifyBleStatus("error", "message", "invalid_json");
      Serial.println("[BLE LỖI]: JSON không hợp lệ");
      return;
    }

    const char* action = doc["action"] | "";
    String actStr = String(action);

    // 1. Nhận cấu hình WiFi
    if (actStr == "set_wifi") {
      const char* ssid = doc["ssid"] | "";
      const char* password = doc["password"] | "";

      if (strlen(ssid) == 0) {
        notifyBleStatus("wifi_failed", "error", "empty_ssid");
        return;
      }

      wifiSSID = String(ssid);
      wifiPassword = String(password);
      saveWiFiConfigToNVS(wifiSSID, wifiPassword);

      if (doc.containsKey("mqtt_host")) {
        const char* mHost = doc["mqtt_host"] | "";
        int mPort = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
        if (strlen(mHost) > 0) {
          if (mqttBrokerIP.fromString(mHost)) {
            mqttBrokerPort = mPort;
            mqttBrokerFound = true;
            saveMQTTConfigToNVS(String(mHost), mPort);
            Serial.printf("[BLE] Nhận cấu hình MQTT: %s:%d\n", mHost, mPort);
          }
        }
      }

      notifyBleStatus("connecting_wifi", "ssid", wifiSSID.c_str());
      shouldConnectWiFi = true;
      Serial.println("[BLE] Đã nhận WiFi credentials, chuẩn bị kết nối...");
      return;
    }
    // 2. Xóa WiFi
    else if (actStr == "reset_wifi") {
      clearStoredWiFi();
      wifiSSID = "";
      wifiPassword = "";
      mqttBrokerFound = false;
      WiFi.disconnect(true);
      mqttClient.disconnect();
      notifyBleStatus("wifi_reset", "message", "config_cleared");
      Serial.println("[BLE] Đã xóa thông tin WiFi trong NVS");
      return;
    }
    // 3. Yêu cầu lấy thông tin thiết bị (Gồm cả AC và TV)
    else if (actStr == "get_info") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument info;
#else
      DynamicJsonDocument info(512);
#endif
      info["status"] = "info";
      info["firmware"] = FIRMWARE_VERSION;
      info["nodeId"] = currentNodeId;
      info["ip"] = WiFi.localIP().toString();
      info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      info["mqtt_connected"] = mqttClient.connected();

      JsonObject acObj = info["ac"].to<JsonObject>();
      acObj["deviceId"] = AC_DEVICE_ID;
      acObj["power"] = acPower;
      acObj["temperature"] = currentTemp;
      acObj["mode"] = modeToString(currentMode);
      acObj["fan"] = fanToString(currentFan);
      acObj["swing"] = currentSwing;

      JsonObject tvObj = info["tv"].to<JsonObject>();
      tvObj["deviceId"] = TV_DEVICE_ID;
      tvObj["power"] = tvPower;
      tvObj["mute"] = tvMute;
      tvObj["volume"] = tvVolume;
      tvObj["channel"] = tvChannel;
      tvObj["lastButton"] = tvLastButton;

      char buf[512];
      serializeJson(info, buf);
      notifyBLE(String(buf));
      return;
    }
    // 4. Lệnh TV trực tiếp qua BLE
    else if (actStr.startsWith("TV_") || actStr == "SEND_BUTTON" || doc.containsKey("button") || String(doc["target"] | "") == "tv") {
      JsonObject obj = doc.as<JsonObject>();
      processTvCommand(actStr, obj, nullptr, TV_DEVICE_ID);
      notifyBleStatus("success", "message", "tv_command_executed");
      return;
    }
    // 5. Lệnh AC trực tiếp qua BLE
    else if (actStr.startsWith("SET_") || actStr == "TOGGLE" || actStr == "POWER" || actStr == "RAW") {
      JsonObject obj = doc.as<JsonObject>();
      processCommand(action, obj, nullptr, AC_DEVICE_ID);
      notifyBleStatus("success", "message", "ac_command_executed");
      return;
    } else {
      notifyBleStatus("error", "message", "unknown_action");
    }
  }
};

// ============================================================
// BLE INITIALIZATION
// ============================================================
void startBLE() {
  Serial.println("[BLE] Dang khoi tao BLE Server...");
  BLEDevice::init(DEVICE_HUB_NAME);
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
  commCharacteristic->setValue("{\"status\":\"ready\",\"device\":\"" DEVICE_HUB_NAME "\"}");
  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setName(DEVICE_HUB_NAME);
  advData.setFlags(0x06);

  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.printf("[BLE] Quang ba BLE thanh cong! Ten: %s | UUID: %s\n", DEVICE_HUB_NAME, BLE_SERVICE_UUID);
}

// ============================================================
// WEB SERVER (TCL TV REMOTE & DAIKIN AC CONTROL UI)
// ============================================================
String buildWebPage() {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>HESTA Remote - TV & AC</title><style>"
               "body{font-family:sans-serif;background:#121212;color:#eee;text-align:center;margin:0;padding:12px}"
               "h2{margin:6px 0;color:#0a84ff;font-size:22px}"
               "h3{margin:8px 0;color:#fff;font-size:18px;border-bottom:1px solid #333;padding-bottom:6px}"
               ".card{background:#1e1e1e;border-radius:12px;padding:12px;margin:10px auto;max-width:440px;box-shadow:0 4px 12px rgba(0,0,0,0.6)}"
               "button{font-size:15px;font-weight:bold;margin:4px;padding:12px 0;width:30%;border:0;border-radius:8px;background:#2a2a2a;color:#fff;cursor:pointer;transition:0.1s}"
               "button:active{background:#0a84ff;transform:scale(0.97)}"
               ".pw{background:#c0392b}"
               ".pw:active{background:#e74c3c}"
               ".mute{background:#8e44ad}"
               ".nav{background:#34495e}"
               ".ac-btn{background:#16a085}"
               ".ac-cool{background:#2980b9}"
               ".status-bar{color:#888;font-size:12px;margin-bottom:8px}"
               ".tv-state{font-size:13px;color:#2ecc71;margin-bottom:8px}"
               "</style></head><body>"
               "<h2>HESTA SMART IR HUB</h2>"
               "<div class='status-bar'>IP: ");
  h += WiFi.localIP().toString();
  h += F(" | Node: ");
  h += currentNodeId;
  h += F("</div><div class='card'><h3>TCL TV REMOTE</h3>"
         "<div class='tv-state'>Trạng thái: <b>");
  h += (tvPower ? "BẬT (ON)" : "TẮT (OFF)");
  h += F("</b> | Âm lượng: <b>");
  h += String(tvVolume);
  h += F("</b> | Kênh: <b>");
  h += String(tvChannel);
  h += F("</b></div>");

  for (int i = 0; i < N_TV_BUTTONS; i++) {
    String bName = String(TV_BUTTONS[i].name);
    h += "<button";
    if (bName == "POWER") h += " class='pw'";
    else if (bName == "MUTE") h += " class='mute'";
    else if (bName == "OK" || bName == "UP" || bName == "DOWN" || bName == "LEFT" || bName == "RIGHT") h += " class='nav'";
    h += " onclick=\"fetch('/send?b=";
    h += bName;
    h += "').then(r=>location.reload())\">";
    h += bName;
    h += "</button>";
    if (i == 2 || i == 6 || i == 11 || i == 14) h += "<br>";
  }

  h += F("</div><div class='card'><h3>DAIKIN AC CONTROL</h3>"
         "<div class='tv-state'>Trạng thái: <b>");
  h += (acPower ? "BẬT (ON)" : "TẮT (OFF)");
  h += F("</b> | Nhiệt độ: <b>");
  h += String(currentTemp);
  h += F("&deg;C</b> | Mode: <b>");
  h += modeToString(currentMode);
  h += F("</b> | Quạt: <b>");
  h += fanToString(currentFan);
  h += F("</b></div>"
         "<button class='ac-btn pw' onclick=\"fetch('/ac?action=POWER').then(r=>location.reload())\">AC POWER</button>"
         "<button class='ac-btn' onclick=\"fetch('/ac?action=TEMP_DOWN').then(r=>location.reload())\">TEMP -</button>"
         "<button class='ac-btn' onclick=\"fetch('/ac?action=TEMP_UP').then(r=>location.reload())\">TEMP +</button><br>"
         "<button class='ac-btn ac-cool' onclick=\"fetch('/ac?action=COOL').then(r=>location.reload())\">COOL 26&deg;C</button>"
         "<button class='ac-btn' onclick=\"fetch('/ac?action=FAN').then(r=>location.reload())\">FAN AUTO</button>"
         "<button class='ac-btn' onclick=\"fetch('/ac?action=SWING').then(r=>location.reload())\">SWING</button>"
         "</div></body></html>");
  return h;
}

void handleRoot() {
  webServer.send(200, "text/html", buildWebPage());
}

void handleWebSend() {
  if (webServer.hasArg("b")) {
    String btn = webServer.arg("b");
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(256);
#endif
    doc["button"] = btn;
    JsonObject obj = doc.as<JsonObject>();
    processTvCommand(btn, obj, nullptr, TV_DEVICE_ID);
    webServer.send(200, "text/plain", "OK: Da gui TV button " + btn);
  } else {
    webServer.send(400, "text/plain", "Thieu tham so b");
  }
}

void handleWebAc() {
  if (webServer.hasArg("action")) {
    String act = webServer.arg("action");
    act.toUpperCase();
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(256);
#endif
    JsonObject obj = doc.as<JsonObject>();

    if (act == "POWER") {
      processCommand("TOGGLE", obj, nullptr, AC_DEVICE_ID);
    } else if (act == "TEMP_UP") {
      processCommand("TEMPERATURE_PLUS", obj, nullptr, AC_DEVICE_ID);
    } else if (act == "TEMP_DOWN") {
      processCommand("TEMPERATURE_MINUS", obj, nullptr, AC_DEVICE_ID);
    } else if (act == "COOL") {
      doc["mode"] = "COOL";
      doc["temp"] = 26;
      processCommand("SET_MODE", obj, nullptr, AC_DEVICE_ID);
    } else if (act == "FAN") {
      doc["fan"] = "AUTO";
      processCommand("SET_FAN", obj, nullptr, AC_DEVICE_ID);
    } else if (act == "SWING") {
      doc["swing"] = !currentSwing;
      processCommand("SET_SWING", obj, nullptr, AC_DEVICE_ID);
    } else {
      processCommand(act.c_str(), obj, nullptr, AC_DEVICE_ID);
    }
    webServer.send(200, "text/plain", "OK: Da thuc hien AC action " + act);
  } else {
    webServer.send(400, "text/plain", "Thieu tham so action");
  }
}

void handleWebStatus() {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(512);
#endif
  doc["nodeId"] = currentNodeId;
  doc["ip"] = WiFi.localIP().toString();

  JsonObject acObj = doc["ac"].to<JsonObject>();
  acObj["deviceId"] = AC_DEVICE_ID;
  acObj["power"] = acPower;
  acObj["temperature"] = currentTemp;
  acObj["mode"] = modeToString(currentMode);
  acObj["fan"] = fanToString(currentFan);
  acObj["swing"] = currentSwing;

  JsonObject tvObj = doc["tv"].to<JsonObject>();
  tvObj["deviceId"] = TV_DEVICE_ID;
  tvObj["power"] = tvPower;
  tvObj["mute"] = tvMute;
  tvObj["volume"] = tvVolume;
  tvObj["channel"] = tvChannel;
  tvObj["lastButton"] = tvLastButton;

  char buf[512];
  serializeJson(doc, buf);
  webServer.send(200, "application/json", buf);
}

void setupWebServer() {
  webServer.on("/", handleRoot);
  webServer.on("/send", handleWebSend);
  webServer.on("/ac", handleWebAc);
  webServer.on("/status", handleWebStatus);
  webServer.begin();
  Serial.printf("[HTTP SERVER] Đã khởi chạy Web Server tại: http://%s/\n", WiFi.localIP().toString().c_str());
}

// ============================================================
// WIFI CONNECTION & PROVISIONING (CHUẨN SYNA)
// ============================================================
bool connectWiFi() {
  Serial.println();
  Serial.println("==================================================");
  Serial.printf("[WIFI] DANG KET NOI WIFI: %s ...\n", wifiSSID.c_str());
  Serial.println("==================================================");

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println("\n[WIFI ERROR] Ket noi WiFi timeout (Sai SSID/Mat khau hoac song yeu)!");
      notifyBleStatus("wifi_failed", "error", "timeout");
      return false;
    }
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.println("[WIFI] DA KET NOI WIFI THANH CONG!");
  Serial.printf(" - Dia chi IP ESP32: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf(" - Do manh tin hieu: %d dBm\n", WiFi.RSSI());
  Serial.println("==================================================");

  saveWiFiConfigToNVS(wifiSSID, wifiPassword);
  notifyBleStatus("wifi_connected", "ip", WiFi.localIP().toString().c_str());
  setupWebServer();
  return true;
}

void handleWiFiProvisioning() {
  connectWiFi();
}

// ============================================================
// mDNS AUTO-DISCOVERY (MQTT BROKER)
// ============================================================
bool discoverMQTTBroker() {
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.println();
  Serial.println("[mDNS] Dang do tim Mosquitto Broker (_syna-mqtt._tcp)...");

  String mdnsHost = "syna-hub-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!MDNS.begin(mdnsHost.c_str())) {
    Serial.println("[mDNS ERROR] Khong the khoi dong mDNS responder!");
  }

  int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);
  if (count <= 0) {
    count = MDNS.queryService("_syna-mqtt", "_tcp");
  }

  if (count > 0) {
    mqttBrokerIP = MDNS.address(0);
    mqttBrokerPort = MDNS.port(0);
    mqttBrokerFound = true;

    Serial.println("--------------------------------------------------");
    Serial.printf("[mDNS THÀNH CÔNG] Tim thay %d dich vu MQTT Broker!\n", count);
    Serial.printf(" -> Broker IP   : %s\n", mqttBrokerIP.toString().c_str());
    Serial.printf(" -> Broker Port : %d\n", mqttBrokerPort);
    Serial.println("--------------------------------------------------");

    saveMQTTConfigToNVS(mqttBrokerIP.toString(), mqttBrokerPort);
    notifyBleStatus("mdns_discovered", "broker", mqttBrokerIP.toString().c_str(), "port", String(mqttBrokerPort).c_str());
    return true;
  }

  Serial.println("[mDNS CẢNH BÁO] Khong tim thay MQTT qua mDNS. Dung Fallback IP:");
  mqttBrokerIP.fromString(FALLBACK_MQTT_IP);
  mqttBrokerPort = DEFAULT_MQTT_PORT;
  Serial.printf(" -> Fallback Broker IP: %s:%d\n", FALLBACK_MQTT_IP, DEFAULT_MQTT_PORT);
  return false;
}

bool discoverMQTT() {
  return discoverMQTTBroker();
}

// ============================================================
// CONNECT MQTT (CHUẨN SYNA / HESTA)
// ============================================================
bool connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!mqttBrokerFound && mqttBrokerIP == INADDR_NONE) {
    discoverMQTTBroker();
  }

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);
  mqttClient.setSocketTimeout(1); // Tránh để PubSubClient block loop() 15s khi broker offline

  Serial.println();
  Serial.printf("[MQTT] Dang ket noi toi Broker: %s:%d ...\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);

  String clientId = "HESTA-IR-HUB-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  // Chuẩn bị LWT (Last Will and Testament) topic cho node
  String lwtTopic = "hesta/nodes/" + currentNodeId + "/status";
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
    Serial.printf(" - Client ID: %s\n", clientId.c_str());
    Serial.println("==================================================");

    // Báo trạng thái Node Online
    mqttClient.publish(lwtTopic.c_str(), "ONLINE", true);

    // 1. Subscribe wildcard topic của Node chính
    String nodeWildcard = "hesta/nodes/" + currentNodeId + "/devices/+/command";
    String nodeCmdTopic = "hesta/nodes/" + currentNodeId + "/command";
    mqttClient.subscribe(nodeWildcard.c_str(), 1);
    mqttClient.subscribe(nodeCmdTopic.c_str(), 1);

    // 2. Subscribe wildcard cho từng Device ID (bất kể Node ID nào từ Backend/App gửi tới)
    String subAcWild   = "hesta/nodes/+/devices/" + String(AC_DEVICE_ID) + "/command";
    String subAcDirect = "hesta/devices/" + String(AC_DEVICE_ID) + "/command";
    String subTvWild   = "hesta/nodes/+/devices/" + String(TV_DEVICE_ID) + "/command";
    String subTvDirect = "hesta/devices/" + String(TV_DEVICE_ID) + "/command";
    mqttClient.subscribe(subAcWild.c_str(), 1);
    mqttClient.subscribe(subAcDirect.c_str(), 1);
    mqttClient.subscribe(subTvWild.c_str(), 1);
    mqttClient.subscribe(subTvDirect.c_str(), 1);

    Serial.printf("[MQTT SUB] %s\n", nodeWildcard.c_str());
    Serial.printf("[MQTT SUB] %s\n", nodeCmdTopic.c_str());
    Serial.printf("[MQTT SUB] %s\n", subAcWild.c_str());
    Serial.printf("[MQTT SUB] %s\n", subTvWild.c_str());

    // Báo trạng thái ONLINE cho cả 2 thiết bị
    publishStatus("ONLINE");
    publishTVStatus("ONLINE");

    // Báo State ban đầu của AC và TV
    publishACState();
    publishTVState();

    // Gửi gói Node Discovery và Telemetry ban đầu
    publishNodeDiscovery();
    publishTelemetry();

    notifyBleStatus("mqtt_connected");
    return true;
  } else {
    Serial.printf("[MQTT ERROR] Ket noi that bai, ma loi rc=%d. Thu lai sau...\n", mqttClient.state());
    return false;
  }
}

// ============================================================
// NODE DISCOVERY (CHUẨN SYNA BACKEND)
// ============================================================
void publishNodeDiscovery() {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/discovery";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(1536);
#endif

  doc["nodeId"]          = currentNodeId;
  doc["deviceName"]      = DEVICE_HUB_NAME;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["macAddress"]      = WiFi.macAddress();
  doc["ipAddress"]       = WiFi.localIP().toString();
  doc["status"]          = "ONLINE";

  JsonArray devList = doc["devices"].to<JsonArray>();

  // 1. Máy lạnh Daikin
  JsonObject dAc = devList.add<JsonObject>();
  dAc["deviceId"]    = AC_DEVICE_ID;
  dAc["name"]        = "Máy lạnh Daikin";
  dAc["type"]        = AC_DEVICE_TYPE;
  dAc["power"]       = acPower;
  dAc["temperature"] = currentTemp;
  dAc["mode"]        = modeToString(currentMode);
  dAc["fan"]         = fanToString(currentFan);
  dAc["swing"]       = currentSwing;
  dAc["gpio"]        = IR_LED_PIN;

  // 2. Ti vi TCL
  JsonObject dTv = devList.add<JsonObject>();
  dTv["deviceId"]    = TV_DEVICE_ID;
  dTv["name"]        = "TV TCL";
  dTv["type"]        = TV_DEVICE_TYPE;
  dTv["power"]       = tvPower;
  dTv["mute"]        = tvMute;
  dTv["volume"]      = tvVolume;
  dTv["channel"]     = tvChannel;
  dTv["gpio"]        = IR_LED_PIN;

  char buf[1536];
  serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT DISCOVERY]: %s\n", buf);
}

// ============================================================
// MQTT PUBLISHERS (STATUS, STATE, ACK, TELEMETRY)
// ============================================================
void publishStatus(const char* status) {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(AC_DEVICE_ID) + "/status";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = AC_DEVICE_ID;
  doc["status"] = status;
  doc["deviceType"] = AC_DEVICE_TYPE;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATUS AC]: %s\n", buf);
}

void publishTVStatus(const char* status) {
  if (!mqttClient.connected()) return;

  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(TV_DEVICE_ID) + "/status";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = TV_DEVICE_ID;
  doc["status"] = status;
  doc["deviceType"] = TV_DEVICE_TYPE;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATUS TV]: %s\n", buf);
}

void publishACState(const char* targetDevId) {
  if (!mqttClient.connected()) return;

  String devId = (targetDevId && strlen(targetDevId) > 0) ? String(targetDevId) : String(AC_DEVICE_ID);
  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + devId + "/state";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = devId;
  doc["deviceType"] = AC_DEVICE_TYPE;
  doc["power"] = acPower;
  doc["temperature"] = currentTemp;
  doc["mode"] = modeToString(currentMode);
  doc["fan"] = fanToString(currentFan);
  doc["swing"] = currentSwing;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATE AC]: %s\n", buf);
}

void publishACACK(const char* commandId, const char* targetDevId) {
  if (!mqttClient.connected() || commandId == nullptr || strlen(commandId) == 0) return;

  String devId = (targetDevId && strlen(targetDevId) > 0) ? String(targetDevId) : String(AC_DEVICE_ID);
  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + devId + "/ack";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(384);
#endif
  doc["commandId"] = commandId;
  doc["deviceId"] = devId;
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
  Serial.printf("[MQTT ACK AC]: %s\n", buf);
}

void publishTVState(const char* targetDevId) {
  if (!mqttClient.connected()) return;

  String devId = (targetDevId && strlen(targetDevId) > 0) ? String(targetDevId) : String(TV_DEVICE_ID);
  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + devId + "/state";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif
  doc["deviceId"] = devId;
  doc["deviceType"] = TV_DEVICE_TYPE;
  doc["power"] = tvPower;
  doc["state"] = tvPower ? "ON" : "OFF";
  doc["mute"] = tvMute;
  doc["volume"] = tvVolume;
  doc["channel"] = tvChannel;
  doc["lastButton"] = tvLastButton;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf, true);
  Serial.printf("[MQTT STATE TV]: %s\n", buf);
}

void publishTVACK(const char* commandId, const char* targetDevId, const char* status) {
  if (!mqttClient.connected() || commandId == nullptr || strlen(commandId) == 0) return;

  String devId = (targetDevId && strlen(targetDevId) > 0) ? String(targetDevId) : String(TV_DEVICE_ID);
  String topic = "hesta/nodes/" + currentNodeId + "/devices/" + devId + "/ack";

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(384);
#endif
  doc["commandId"] = commandId;
  doc["deviceId"] = devId;
  doc["status"] = (status != nullptr) ? status : "SUCCESS";

  JsonObject st = doc["state"].to<JsonObject>();
  st["power"] = tvPower;
  st["state"] = tvPower ? "ON" : "OFF";
  st["mute"] = tvMute;
  st["volume"] = tvVolume;
  st["channel"] = tvChannel;
  st["lastButton"] = tvLastButton;

  char buf[384];
  serializeJson(doc, buf);
  mqttClient.publish(topic.c_str(), buf);
  Serial.printf("[MQTT ACK TV]: %s\n", buf);
}

void publishTelemetry() {
  if (!mqttClient.connected()) return;

  // 1. Telemetry Điều hòa
  {
    String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(AC_DEVICE_ID) + "/telemetry";
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(384);
#endif
    doc["deviceId"] = AC_DEVICE_ID;
    doc["deviceType"] = AC_DEVICE_TYPE;
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
  }

  // 2. Telemetry TV
  {
    String topic = "hesta/nodes/" + currentNodeId + "/devices/" + String(TV_DEVICE_ID) + "/telemetry";
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(384);
#endif
    doc["deviceId"] = TV_DEVICE_ID;
    doc["deviceType"] = TV_DEVICE_TYPE;
    doc["power"] = tvPower;
    doc["mute"] = tvMute;
    doc["volume"] = tvVolume;
    doc["channel"] = tvChannel;
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;

    char buf[384];
    serializeJson(doc, buf);
    mqttClient.publish(topic.c_str(), buf);
  }

  Serial.println("[MQTT TELEMETRY] Đã gửi telemetry định kỳ cho cả AC và TV.");
}

// ============================================================
// TV COMMAND PROCESSOR (XỬ LÝ LỆNH ĐIỀU KHIỂN TV)
// ============================================================
void processTvCommand(String act, JsonObject& doc, const char* commandId, const char* reqDeviceId) {
  const char* replyDeviceId = (reqDeviceId && strlen(reqDeviceId) > 0) ? reqDeviceId : TV_DEVICE_ID;
  bool success = false;
  String buttonSent = "";

  act.trim();
  act.toUpperCase();

  // Bỏ tiền tố TV_ nếu có để xử lý đồng nhất
  if (act.startsWith("TV_")) {
    act = act.substring(3);
  }

  Serial.printf("[TV COMMAND] Xử lý Action: %s cho DeviceId: %s\n", act.c_str(), replyDeviceId);

  // 1. NGUỒN: BẬT / TẮT / TOGGLE
  if (act == "POWER" || act == "TOGGLE" || act == "TV_POWER") {
    success = sendTvButtonByName("POWER");
    if (success) {
      tvPower = !tvPower;
      buttonSent = "POWER";
    }
  }
  else if (act == "POWER_ON" || act == "TURN_ON" || act == "ON") {
    if (!tvPower) {
      success = sendTvButtonByName("POWER");
      tvPower = true;
      buttonSent = "POWER";
    } else {
      bool force = doc["force"] | false;
      if (force) {
        success = sendTvButtonByName("POWER");
        buttonSent = "POWER";
      } else {
        success = true; // TV đã bật sẵn
      }
    }
  }
  else if (act == "POWER_OFF" || act == "TURN_OFF" || act == "OFF") {
    if (tvPower) {
      success = sendTvButtonByName("POWER");
      tvPower = false;
      buttonSent = "POWER";
    } else {
      bool force = doc["force"] | false;
      if (force) {
        success = sendTvButtonByName("POWER");
        buttonSent = "POWER";
      } else {
        success = true; // TV đã tắt sẵn
      }
    }
  }
  else if (act == "SET_POWER") {
    bool targetPower = true;
    if (doc.containsKey("power")) {
      if (doc["power"].is<bool>()) {
        targetPower = doc["power"].as<bool>();
      } else if (doc["power"].is<const char*>()) {
        String pStr = doc["power"].as<const char*>();
        pStr.toUpperCase();
        targetPower = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
      }
    }
    if (targetPower != tvPower) {
      success = sendTvButtonByName("POWER");
      tvPower = targetPower;
      buttonSent = "POWER";
    } else {
      success = true;
    }
  }
  // 2. ÂM LƯỢNG: TĂNG / GIẢM / TẮT TIẾNG (MUTE)
  else if (act == "VOL_UP" || act == "VOLUME_UP" || act == "VOL+") {
    success = sendTvButtonByName("VOL_UP");
    if (success) {
      buttonSent = "VOL_UP";
      if (tvVolume < 100) tvVolume++;
      tvMute = false;
    }
  }
  else if (act == "VOL_DOWN" || act == "VOLUME_DOWN" || act == "VOL-") {
    success = sendTvButtonByName("VOL_DOWN");
    if (success) {
      buttonSent = "VOL_DOWN";
      if (tvVolume > 0) tvVolume--;
    }
  }
  else if (act == "MUTE") {
    success = sendTvButtonByName("MUTE");
    if (success) {
      buttonSent = "MUTE";
      tvMute = !tvMute;
    }
  }
  // 3. KÊNH: TĂNG / GIẢM / CHUYỂN KÊNH TRỰC TIẾP
  else if (act == "CH_UP" || act == "CHANNEL_UP" || act == "CH+") {
    success = sendTvButtonByName("CH_UP");
    if (success) {
      buttonSent = "CH_UP";
      tvChannel++;
    }
  }
  else if (act == "CH_DOWN" || act == "CHANNEL_DOWN" || act == "CH-") {
    success = sendTvButtonByName("CH_DOWN");
    if (success) {
      buttonSent = "CH_DOWN";
      if (tvChannel > 1) tvChannel--;
    }
  }
  else if (act == "SET_CHANNEL") {
    int ch = doc["channel"] | 0;
    if (ch > 0) {
      String chStr = String(ch);
      success = true;
      for (size_t i = 0; i < chStr.length(); i++) {
        String digit = String(chStr[i]);
        sendTvButtonByName(digit);
        delay(120);
      }
      tvChannel = ch;
      buttonSent = chStr;
    }
  }
  // 4. PHÍM ĐIỀU HƯỚNG D-PAD & CHỨC NĂNG
  else if (act == "UP" || act == "DOWN" || act == "LEFT" || act == "RIGHT" ||
           act == "OK" || act == "ENTER" || act == "BACK" || act == "RETURN" ||
           act == "HOME" || act == "MENU" || act == "INPUT" || act == "SOURCE") {
    String btn = act;
    if (btn == "ENTER") btn = "OK";
    if (btn == "RETURN") btn = "BACK";
    if (btn == "SOURCE") btn = "INPUT";
    success = sendTvButtonByName(btn);
    if (success) buttonSent = btn;
  }
  // 5. CÁC NÚT SỐ 0 - 9
  else if (act.length() == 1 && act[0] >= '0' && act[0] <= '9') {
    success = sendTvButtonByName(act);
    if (success) buttonSent = act;
  }
  // 6. GỬI TÊN NÚT QUA SEND_BUTTON / PRESS_BUTTON / KEY
  else if (act == "SEND_BUTTON" || act == "PRESS_BUTTON" || act == "BUTTON" || act == "KEY") {
    String btn = "";
    if (doc.containsKey("button")) btn = doc["button"].as<String>();
    else if (doc.containsKey("key")) btn = doc["key"].as<String>();
    else if (doc.containsKey("name")) btn = doc["name"].as<String>();

    if (btn.length() > 0) {
      success = sendTvButtonByName(btn);
      if (success) buttonSent = btn;
    }
  }
  // 7. PHÁT MÃ XUNG THÔ (RAW / NIKAI CODE)
  else if (act == "SEND_IR_CODE" || act == "SEND_IR" || act == "IR_CODE") {
    uint32_t code = 0;
    if (doc.containsKey("code")) {
      if (doc["code"].is<const char*>()) {
        String codeStr = doc["code"].as<const char*>();
        code = strtoul(codeStr.c_str(), NULL, 0);
      } else {
        code = doc["code"].as<uint32_t>();
      }
    }
    if (code > 0) {
      success = sendTvCode(code);
      buttonSent = "RAW_CODE";
    }
  }
  // 8. LẤY TRẠNG THÁI HIỆN TẠI (GET_STATE)
  else if (act == "GET_STATE") {
    publishTVState(replyDeviceId);
    publishTVACK(commandId, replyDeviceId, "SUCCESS");
    return;
  }
  // 9. Thử tìm trực tiếp theo tên nút bất kỳ trong danh sách
  else {
    success = sendTvButtonByName(act);
    if (success) buttonSent = act;
  }

  if (buttonSent.length() > 0) {
    tvLastButton = buttonSent;
  }

  saveStoredTVState();

  // Luôn phản hồi STATE và ACK về Backend
  publishTVState(replyDeviceId);
  if (String(replyDeviceId) != String(TV_DEVICE_ID)) {
    // Nếu lệnh gửi qua AC Device ID kèm target: tv, cũng cập nhật topic của TV
    publishTVState(TV_DEVICE_ID);
  }

  if (commandId && strlen(commandId) > 0) {
    publishTVACK(commandId, replyDeviceId, success ? "SUCCESS" : "FAILED");
  }
}

// ============================================================
// AC COMMAND PROCESSOR (XỬ LÝ LỆNH ĐIỀU HÒA DAIKIN)
// ============================================================
void processCommand(const char* action, JsonObject& doc, const char* commandId, const char* reqDeviceId) {
  if (!action) return;

  const char* replyDeviceId = (reqDeviceId && strlen(reqDeviceId) > 0) ? reqDeviceId : AC_DEVICE_ID;
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
      Serial.printf("[AC LỖI]: Nhiệt độ %d°C ngoài dải cho phép (16-30°C)\n", t);
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
    publishACACK(commandId, replyDeviceId);
    return;
  }
  // 9. LỆNH LẤY TRẠNG THÁI (GET_STATE)
  else if (act == "GET_STATE") {
    publishACState(replyDeviceId);
    publishACACK(commandId, replyDeviceId);
    return;
  }
  else {
    Serial.printf("[AC MQTT]: Lệnh không xác định cho AC: %s\n", act.c_str());
  }

  if (needSendIR) {
    sendIRState(togglePowerBit);
  }

  publishACState(replyDeviceId);
  publishACACK(commandId, replyDeviceId);
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

  // Tự động trích xuất nodeId từ topic nếu có: hesta/nodes/{nodeId}/devices/...
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
    // Xử lý chuỗi text thô
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
    } else if (raw == "tv_power" || raw == "tv_on" || raw == "tv_off") {
      sendTvButtonByName("POWER");
      tvPower = !tvPower;
      publishTVState();
    } else if (raw.startsWith("tv_")) {
      sendTvButtonByName(raw.substring(3));
      publishTVState();
    }
    return;
  }

  JsonObject obj = doc.as<JsonObject>();
  const char* commandId = doc["commandId"] | "";
  const char* action    = doc["action"] | "";
  const char* target    = doc["target"] | "";
  const char* devId     = doc["deviceId"] | "";
  const char* devType   = doc["deviceType"] | "";

  // Trích xuất deviceId từ topic nếu trong payload không có
  String targetDeviceId = String(devId);
  if (targetDeviceId.isEmpty()) {
    int devIdx = topicStr.indexOf("/devices/");
    if (devIdx >= 0) {
      int start = devIdx + 9;
      int end = topicStr.indexOf("/command", start);
      if (end > start) {
        targetDeviceId = topicStr.substring(start, end);
      }
    }
  }

  // Phân biệt lệnh cho TV hay Máy Lạnh
  bool isTvTarget = false;

  // 1. Dựa trên Device ID hoặc Topic
  if (targetDeviceId.equalsIgnoreCase(TV_DEVICE_ID) || topicStr.indexOf(TV_DEVICE_ID) >= 0 || topicStr.indexOf("/tv/") >= 0) {
    isTvTarget = true;
  }

  // 2. Dựa trên trường target / deviceType trong JSON
  if (String(target).equalsIgnoreCase("tv") || String(devType).equalsIgnoreCase("tv")) {
    isTvTarget = true;
  }

  // 3. Dựa trên tên Action đặc trưng của TV
  String actStr = String(action);
  actStr.toUpperCase();
  if (actStr.startsWith("TV_") || actStr == "SEND_BUTTON" || actStr == "PRESS_BUTTON" ||
      actStr == "VOL_UP" || actStr == "VOL_DOWN" || actStr == "VOLUME_UP" || actStr == "VOLUME_DOWN" ||
      actStr == "MUTE" || actStr == "CH_UP" || actStr == "CH_DOWN" || actStr == "CHANNEL_UP" || actStr == "CHANNEL_DOWN" ||
      actStr == "SET_CHANNEL" || actStr == "INPUT" || actStr == "HOME" || actStr == "MENU" || actStr == "BACK" ||
      actStr == "UP" || actStr == "DOWN" || actStr == "LEFT" || actStr == "RIGHT" || actStr == "OK" ||
      (actStr.length() == 1 && actStr[0] >= '0' && actStr[0] <= '9')) {
    isTvTarget = true;
  }

  // 4. Dựa trên các tham số đặc trưng của TV trong payload
  if (doc.containsKey("button") || doc.containsKey("key") || doc.containsKey("channel")) {
    isTvTarget = true;
  }

  // Nếu action rỗng nhưng có tham số trực tiếp
  if (strlen(action) == 0) {
    if (doc.containsKey("button") || doc.containsKey("channel")) {
      action = "SEND_BUTTON";
      isTvTarget = true;
    } else if (doc.containsKey("power")) {
      action = "SET_POWER";
    } else if (doc.containsKey("temperature") || doc.containsKey("temp")) {
      action = "SET_TEMPERATURE";
    } else if (doc.containsKey("fan")) {
      action = "SET_FAN";
    } else if (doc.containsKey("mode")) {
      action = "SET_MODE";
    } else if (doc.containsKey("swing")) {
      action = "SET_SWING";
    }
  }

  // Thực thi lệnh tương ứng
  if (isTvTarget) {
    processTvCommand(String(action), obj, commandId, targetDeviceId.c_str());
  } else {
    processCommand(action, obj, commandId, targetDeviceId.c_str());
  }
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

  // --- LỆNH TV ---
  if (lowerCmd.startsWith("tv ")) {
    String sub = cmd.substring(3);
    sub.trim();
    sub.toUpperCase();
    if (sub == "POWER" || sub == "TOGGLE") {
      sendTvButtonByName("POWER");
      tvPower = !tvPower;
      publishTVState();
    } else if (sub == "ON") {
      if (!tvPower) { sendTvButtonByName("POWER"); tvPower = true; }
      publishTVState();
    } else if (sub == "OFF") {
      if (tvPower) { sendTvButtonByName("POWER"); tvPower = false; }
      publishTVState();
    } else {
      sendTvButtonByName(sub);
      publishTVState();
    }
    return;
  }

  // Các shortcut TV phổ biến không cần gõ tiền tố "tv "
  if (lowerCmd == "vol_up" || lowerCmd == "vol+" || lowerCmd == "vol_down" || lowerCmd == "vol-" ||
      lowerCmd == "mute" || lowerCmd == "ch_up" || lowerCmd == "ch+" || lowerCmd == "ch_down" || lowerCmd == "ch-") {
    String btn = lowerCmd;
    btn.toUpperCase();
    sendTvButtonByName(btn);
    publishTVState();
    return;
  }

  // --- LỆNH MÁY LẠNH (AC) ---
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
  else if (lowerCmd == "mode offline" || lowerCmd == "offline") {
    offlineTestMode = true;
    Serial.println("[MODE] Da chuyen sang che do TEST OFFLINE (bo qua mang).");
  }
  else if (lowerCmd == "mode online" || lowerCmd == "online") {
    offlineTestMode = false;
    Serial.println("[MODE] Da chuyen sang che do ONLINE. Dang ket noi mang...");
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      discoverMQTTBroker();
      connectMQTT();
    }
  }
  else if (lowerCmd == "reset_wifi") {
    clearStoredWiFi();
    WiFi.disconnect(true);
    mqttClient.disconnect();
    Serial.println("[RESET]: Đã xóa cấu hình WiFi/MQTT trong NVS.");
  }
  else if (lowerCmd == "mdns") {
    Serial.println("[mDNS]: Kích hoạt tìm lại Broker...");
    mqttBrokerFound = false;
    if (discoverMQTTBroker()) connectMQTT();
  }
  else {
    Serial.println("Lệnh Serial không hợp lệ. Các lệnh hỗ trợ:");
    Serial.println("  [AC] : power, on, off, t16..t30, cool, heat, dry, fan, fauto, f1..f3, swing1, swing0, raw");
    Serial.println("  [TV] : tv power, tv on, tv off, tv vol_up, tv vol_down, tv mute, tv ch_up, tv 0..9, tv <button>");
    Serial.println("  [HỆ THỐNG] : status, reset_wifi, mdns, mode offline, mode online");
  }
}

void printStatus() {
  Serial.println();
  Serial.println("================= TRẠNG THÁI HIỆN TẠI (COMBO HUB) =================");
  Serial.printf("Node ID       : %s\n", currentNodeId.c_str());
  Serial.printf("Chế độ (Mode) : %s\n", offlineTestMode ? "TEST OFFLINE (SERIAL ONLY)" : "ONLINE (WIFI + MQTT)");
  Serial.printf("WiFi          : %s (IP: %s, RSSI: %d dBm)\n",
                (WiFi.status() == WL_CONNECTED) ? "ĐÃ KẾT NỐI" : "CHƯA KẾT NỐI",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  Serial.printf("MQTT Broker   : %s:%d (%s)\n",
                mqttBrokerIP.toString().c_str(),
                mqttBrokerPort,
                mqttClient.connected() ? "ĐÃ KẾT NỐI" : "MẤT KẾT NỐI");
  Serial.printf("BLE Status    : %s (Name: %s)\n", bleClientConnected ? "APP ĐÃ KẾT NỐI" : "ĐANG PHÁT QUẢNG BÁ", DEVICE_HUB_NAME);
  Serial.println("----------------- 1. MÁY LẠNH DAIKIN (AC) -----------------");
  Serial.printf("AC Device ID  : %s\n", AC_DEVICE_ID);
  Serial.printf("Nguồn (AC)    : %s\n", acPower ? "ĐANG BẬT (ON)" : "ĐANG TẮT (OFF)");
  Serial.printf("Nhiệt độ      : %d °C\n", currentTemp);
  Serial.printf("Chế độ (Mode) : %s\n", modeToString(currentMode));
  Serial.printf("Quạt (Fan)    : %s\n", fanToString(currentFan));
  Serial.printf("Đảo gió       : %s\n", currentSwing ? "BẬT" : "TẮT");
  Serial.println("----------------- 2. TI VI TCL (TV) -----------------------");
  Serial.printf("TV Device ID  : %s\n", TV_DEVICE_ID);
  Serial.printf("Nguồn (TV)    : %s\n", tvPower ? "ĐANG BẬT (ON)" : "ĐANG TẮT (OFF)");
  Serial.printf("Tắt tiếng     : %s\n", tvMute ? "ĐANG TẮT TIẾNG (MUTED)" : "BẬT TIẾNG");
  Serial.printf("Âm lượng      : %d\n", tvVolume);
  Serial.printf("Kênh hiện tại : %d\n", tvChannel);
  Serial.printf("Nút bấm gần nhất: %s\n", tvLastButton.c_str());
  Serial.println("===================================================================");
}

// ============================================================
// ARDUINO SETUP & LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n====================================================================");
  Serial.println("  HESTA / SYNA SMART HOME - DAIKIN AC & TCL TV COMBO HUB");
  Serial.println("  NODE ID : " NODE_ID);
  Serial.println("  FIRMWARE: " FIRMWARE_VERSION);
  Serial.println("====================================================================");
  Serial.printf("   AC Device ID: %s\n", AC_DEVICE_ID);
  Serial.printf("   TV Device ID: %s\n", TV_DEVICE_ID);
  Serial.printf("   IR LED Pin  : %d (Shared 2N2222 Transistor)\n", IR_LED_PIN);
  Serial.println("====================================================================");

  // Khởi động phần cứng IR
  ac.begin();
  irsend.begin();

  // Nạp trạng thái AC, TV và cấu hình WiFi/MQTT đã lưu trong NVS
  loadStoredACState();
  loadStoredTVState();
  loadNetworkConfigFromNVS();

  // Kiểm tra chế độ chạy (Offline Test vs Online đầy đủ)
  if (offlineTestMode) {
    Serial.println("\n====================================================================");
    Serial.println("[MODE] DANG CHAY CHE DO: TEST OFFLINE (SERIAL ONLY)");
    Serial.println(" -> Da bo qua ket noi Wi-Fi, MQTT va BLE.");
    Serial.println(" -> Go 'mode online' tren Serial de bat lai ket noi mang.");
    Serial.println("====================================================================");
  } else {
    // Khởi tạo BLE Provisioning (hỗ trợ App SYNA)
    startBLE();

    // Kết nối Wi-Fi
    connectWiFi();

    // Dò tìm mDNS và kết nối MQTT Broker
    if (WiFi.status() == WL_CONNECTED) {
      discoverMQTTBroker();
      connectMQTT();
    }
  }

  Serial.println("-> He thong da san sang nhan lenh qua MQTT, Web Server, BLE hoac Serial...\n");
  printStatus();
}

void loop() {
  unsigned long now = millis();

  // 1. Phục vụ Web Server nội bộ nếu WiFi đã kết nối
  if (WiFi.status() == WL_CONNECTED) {
    webServer.handleClient();
  }

  // 2. Quản lý mạng khi KHÔNG ở chế độ Offline Test Mode
  if (!offlineTestMode) {
    // 2.1 Kiểm tra cờ kết nối Wi-Fi khi nhận thông tin từ BLE App
    if (shouldConnectWiFi) {
      shouldConnectWiFi = false;
      if (connectWiFi()) {
        discoverMQTTBroker();
        connectMQTT();
      }
    }

    // 2.2 Tự động kết nối lại Wi-Fi nếu bị rớt mạng
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWiFiReconnectAttempt >= 10000) {
        lastWiFiReconnectAttempt = now;
        Serial.println("[WIFI AUTO-RECONNECT] Dang thu ket noi lai Wi-Fi...");
        if (WiFi.reconnect()) {
          Serial.println("[WIFI AUTO-RECONNECT] Da ket noi lai WiFi!");
        }
      }
    } else {
      // 2.3 Quản lý kết nối MQTT Broker
      if (!mqttClient.connected()) {
        if (now - lastMqttReconnectAttempt >= 5000) {
          lastMqttReconnectAttempt = now;
          Serial.println("[MQTT AUTO-RECONNECT] Mat ket noi MQTT, dang thu ket noi lai...");
          connectMQTT();
        }
      } else {
        mqttClient.loop();

        // 2.4 Gửi Telemetry định kỳ mỗi 30 giây cho cả AC và TV
        if (now - lastTelemetryPublish >= TELEMETRY_INTERVAL_MS) {
          lastTelemetryPublish = now;
          publishTelemetry();
        }
      }
    }
  }

  // 3. Lắng nghe lệnh điều khiển Serial trực tiếp
  handleSerial();

  delay(10);
}
