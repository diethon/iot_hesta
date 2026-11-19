
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

// ============================================================
// ESP32-CAM AI-THINKER
// SLIDING GATE + MQTT
//
// HARDWARE
// ------------------------------------------------------------
// ESP32-CAM
// L298N
// TT Gear Motor
// 2x Limit Switch
//
// PIN
// ------------------------------------------------------------
// GPIO13 -> L298N IN1
// GPIO14 -> L298N IN2
// GPIO12 -> L298N ENA
//
// GPIO15 -> LIMIT OPEN
// GPIO2  -> LIMIT CLOSE
// ============================================================


// ============================================================
// BLE PROVISIONING + MQTT mDNS CONFIGURATION
// BLE app gửi JSON: {"action":"set_wifi","ssid":"...","password":"..."}
// MQTT broker được tìm tự động qua dịch vụ _syna-mqtt._tcp
// ============================================================

#define BLE_SERVICE_UUID "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID "12345678-1234-1234-1234-123456789001"
#define BLE_DEVICE_NAME "SYNA-GATE-CAM"
#define MQTT_DISCOVERY_SERVICE "_syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "_tcp"
#define MQTT_DEFAULT_PORT 1883
#define FALLBACK_MQTT_IP "192.168.137.107"

Preferences preferences;

String wifiSSID = "";
String wifiPassword = "";
bool shouldConnectWiFi = false;
bool bleClientConnected = false;
bool mdnsStarted = false;
bool mqttBrokerFound = false;
IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = MQTT_DEFAULT_PORT;

BLEServer *bleServer = nullptr;
BLECharacteristic *commCharacteristic = nullptr;

// ============================================================
// DEVICE INFORMATION
// ============================================================

const char* NODE_ID = "8d1cdd82-b339-469e-be13-7e91070f7ae5";


// ============================================================
// MQTT TOPICS
// ============================================================

const char* MQTT_COMMAND_TOPIC =
  "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/devices/2d566d0a-f7d6-4fac-a0bf-18912da4ab28/command";

const char* MQTT_STATE_TOPIC =
  "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/devices/2d566d0a-f7d6-4fac-a0bf-18912da4ab28/state";

const char* MQTT_TELEMETRY_TOPIC =
  "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/devices/2d566d0a-f7d6-4fac-a0bf-18912da4ab28/telemetry";


// ============================================================
// ESP32-CAM NETWORK
// ============================================================

WiFiClient espClient;

PubSubClient mqttClient(espClient);


// ============================================================
// PIN CONFIGURATION
// ============================================================

#define MOTOR_IN1       13
#define MOTOR_IN2       14
#define MOTOR_ENA       12

#define LIMIT_OPEN      15
#define LIMIT_CLOSE      2


// ============================================================
// MOTOR CONFIGURATION
// ============================================================

#define MOTOR_SPEED 200

#define LIMIT_PRESSED HIGH


// ============================================================
// GATE STATE
// ============================================================

enum GateState {

  GATE_STOPPED,

  GATE_OPENING,

  GATE_CLOSING,

  GATE_OPEN,

  GATE_CLOSED
};


GateState gateState = GATE_STOPPED;


// ============================================================
// MQTT RECONNECT TIMER
// ============================================================

unsigned long lastMqttReconnectAttempt = 0;

const unsigned long MQTT_RECONNECT_INTERVAL = 5000;


// Forward declarations
void connectWiFi();
bool discoverMQTT();
bool connectMQTT();
void startBLE();
void handleBLEProvisioning();
void loadStoredWiFi();
void saveStoredWiFi(const String& ssid, const String& password);
void clearStoredWiFi();
void loadStoredMQTT();
void saveStoredMQTT(const String& host, uint16_t port);
const char* gateStateToString();

class GateBLEServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleClientConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer* server) override {
    bleClientConnected = false;
    Serial.println("[BLE] Client disconnected; advertising again");
    BLEDevice::startAdvertising();
  }
};

class GateBLECharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue().c_str();
    value.trim();

    if (value.isEmpty()) return;

    Serial.print("[BLE] Received: ");
    Serial.println(value);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      characteristic->setValue("{\"status\":\"error\",\"message\":\"invalid_json\"}");
      characteristic->notify();
      Serial.println("[BLE] Invalid JSON");
      return;
    }

    const char* action = doc["action"] | "";
    if (String(action) == "set_wifi") {
      const char* ssid = doc["ssid"] | "";
      const char* password = doc["password"] | "";

      if (strlen(ssid) == 0) {
        characteristic->setValue("{\"status\":\"error\",\"message\":\"ssid_required\"}");
        characteristic->notify();
        return;
      }

      wifiSSID = String(ssid);
      wifiPassword = String(password);

      // Support optional mqtt_host & mqtt_port from App
      if (doc.containsKey("mqtt_host")) {
        const char* mHost = doc["mqtt_host"] | "";
        int mPort = doc["mqtt_port"] | MQTT_DEFAULT_PORT;
        if (strlen(mHost) > 0) {
          if (mqttBrokerIP.fromString(mHost)) {
            mqttBrokerPort = mPort;
            mqttBrokerFound = true;
            saveStoredMQTT(mHost, mPort);
            Serial.print("[BLE] MQTT Broker from App: ");
            Serial.print(mqttBrokerIP);
            Serial.print(":");
            Serial.println(mqttBrokerPort);
          }
        }
      }

      shouldConnectWiFi = true;
      characteristic->setValue("{\"status\":\"accepted\",\"message\":\"wifi_credentials_received\"}");
      characteristic->notify();
      Serial.println("[BLE] WiFi credentials accepted; connecting shortly");
    } else if (String(action) == "reset_wifi") {
      clearStoredWiFi();
      wifiSSID = "";
      wifiPassword = "";
      mqttBrokerFound = false;
      WiFi.disconnect(true);
      mqttClient.disconnect();
      characteristic->setValue("{\"status\":\"wifi_reset\",\"message\":\"WiFi credentials cleared\"}");
      characteristic->notify();
      Serial.println("[BLE] WiFi credentials cleared");
    } else if (String(action) == "get_info") {
      JsonDocument info;
      info["nodeId"] = NODE_ID;
      info["device"] = "gate";
      info["state"] = gateStateToString();
      info["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      info["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
      info["mqtt_connected"] = mqttClient.connected();
      char buf[256];
      serializeJson(info, buf);
      characteristic->setValue(buf);
      characteristic->notify();
    } else {
      characteristic->setValue("{\"status\":\"error\",\"message\":\"unknown_action\"}");
      characteristic->notify();
    }
  }
};

void startBLE() {
  BLEDevice::init(BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new GateBLEServerCallbacks());

  BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  commCharacteristic = service->createCharacteristic(
    BLE_COMM_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  commCharacteristic->addDescriptor(new BLE2902());
  commCharacteristic->setCallbacks(new GateBLECharacteristicCallbacks());
  commCharacteristic->setValue("{\"status\":\"ready\",\"device\":\"SYNA-GATE-CAM\"}");
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.print("[BLE] Advertising as ");
  Serial.println(BLE_DEVICE_NAME);
}

// ============================================================
// NVS PREFERENCES (LƯU & ĐỌC FLASH)
// ============================================================
void loadStoredWiFi() {
  preferences.begin("wifi", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  if (wifiSSID.length() > 0) {
    Serial.print("[NVS] Tim thay WiFi da luu trong Flash: ");
    Serial.println(wifiSSID);
  } else {
    Serial.println("[NVS] Chua co cau hinh WiFi trong Flash.");
  }
}

void saveStoredWiFi(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  Serial.println("[NVS] Da luu cau hinh WiFi vao bo nho Flash NVS.");
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
      Serial.print("[NVS] Da nap MQTT Broker tu Flash: ");
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
  Serial.println("[NVS] Da luu cau hinh MQTT Broker vao bo nho Flash NVS.");
}

void clearStoredWiFi() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  preferences.begin("mqtt", false);
  preferences.clear();
  preferences.end();

  mqttBrokerFound = false;
  Serial.println("[NVS] Da xoa toan bo cau hinh WiFi & MQTT trong Flash.");
}

bool discoverMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[mDNS] WiFi not connected; cannot discover broker");
    return false;
  }

  if (!mdnsStarted) {
    if (!MDNS.begin("syna-gate-cam")) {
      Serial.println("[mDNS] Failed to start mDNS responder");
      return false;
    }
    mdnsStarted = true;
  }

  Serial.println();
  Serial.println("======================================");
  Serial.println("[mDNS] Dang tim kiem MQTT broker qua DNS-SD...");
  Serial.println("       Service: _syna-mqtt._tcp.local");
  Serial.println("======================================");

  int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);
  if (count <= 0) {
    count = MDNS.queryService("syna-mqtt", "tcp");
  }

  Serial.print("[mDNS] So luong broker tim thay: ");
  Serial.println(count);

  if (count > 0) {
    for (int i = 0; i < count; i++) {
      IPAddress discoveredIP = MDNS.address(i);
      uint16_t discoveredPort = MDNS.port(i);

      if (discoveredIP != IPAddress(0, 0, 0, 0) && discoveredPort > 0) {
        mqttBrokerIP = discoveredIP;
        mqttBrokerPort = discoveredPort;
        mqttBrokerFound = true;

        Serial.println("[mDNS] >>> TIM THAY MQTT BROKER QUA MDNS! <<<");
        Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
        Serial.print("       Port: "); Serial.println(mqttBrokerPort);
        saveStoredMQTT(mqttBrokerIP.toString(), mqttBrokerPort);
        return true;
      }
    }
  }

  Serial.println("[mDNS] Khong tim thay MQTT broker qua mDNS.");

  // Fallback 1: Neu da co IP broker luu trong Flash NVS hoac tu App
  if (mqttBrokerFound && mqttBrokerIP != IPAddress(0, 0, 0, 0)) {
    Serial.println("[mDNS] Su dung MQTT Broker da luu trong Flash/App:");
    Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
    Serial.print("       Port: "); Serial.println(mqttBrokerPort);
    return true;
  }

  // Fallback 2: Uu tien su dung IP Broker cau hinh san (192.168.137.107)
  if (mqttBrokerIP.fromString(FALLBACK_MQTT_IP)) {
    mqttBrokerPort = MQTT_DEFAULT_PORT;
    mqttBrokerFound = true;
    Serial.println("[mDNS] >>> FALLBACK: Uu tien su dung IP Broker cau hinh san (" FALLBACK_MQTT_IP ") <<<");
    Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
    Serial.print("       Port: "); Serial.println(mqttBrokerPort);
    return true;
  }

  // Fallback 3: Tu dong lay Gateway IP (IP may tinh khi phat Windows Hotspot)
  IPAddress gw = WiFi.gatewayIP();
  if (gw != IPAddress(0, 0, 0, 0)) {
    mqttBrokerIP = gw;
    mqttBrokerPort = MQTT_DEFAULT_PORT;
    mqttBrokerFound = true;
    Serial.println("[mDNS] >>> FALLBACK: Tu dong dung Gateway IP lam MQTT Broker <<<");
    Serial.print("       IP  : "); Serial.println(mqttBrokerIP);
    Serial.print("       Port: "); Serial.println(mqttBrokerPort);
    return true;
  }

  return false;
}

void handleBLEProvisioning() {
  if (!shouldConnectWiFi) return;

  // Clear the flag first to prevent reconnecting on every loop iteration.
  shouldConnectWiFi = false;
  mqttClient.disconnect();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    // Luu thong tin WiFi vao Flash NVS khi ket noi thanh cong
    saveStoredWiFi(wifiSSID, wifiPassword);

    if (commCharacteristic != nullptr) {
      String okJson = "{\"status\":\"wifi_connected\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      commCharacteristic->setValue(okJson.c_str());
      commCharacteristic->notify();
    }

    delay(1000);
    mqttBrokerFound = false;
    if (discoverMQTT()) {
      if (connectMQTT()) {
        if (commCharacteristic != nullptr) {
          commCharacteristic->setValue("{\"status\":\"mqtt_connected\"}");
          commCharacteristic->notify();
        }
      }
    }
  } else {
    if (commCharacteristic != nullptr) {
      commCharacteristic->setValue("{\"status\":\"wifi_failed\",\"error\":\"WiFi connection failed\"}");
      commCharacteristic->notify();
    }
  }
}

// ============================================================
// STATE TO STRING
// ============================================================

const char* gateStateToString() {

  switch (gateState) {

    case GATE_STOPPED:
      return "STOPPED";

    case GATE_OPENING:
      return "OPENING";

    case GATE_CLOSING:
      return "CLOSING";

    case GATE_OPEN:
      return "OPEN";

    case GATE_CLOSED:
      return "CLOSED";

    default:
      return "UNKNOWN";
  }
}


// ============================================================
// WIFI CONNECT
// ============================================================

void connectWiFi() {
  if (wifiSSID.isEmpty()) {
    Serial.println("[WiFi] Chua co thong tin WiFi. Hay provision qua BLE.");
    return;
  }

  Serial.println();
  Serial.println("======================================");
  Serial.println("[WiFi] CONNECTING USING BLE CREDENTIALS");
  Serial.print("[WiFi] SSID: ");
  Serial.println(wifiSSID);
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] CONNECTED");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] RSSI: ");
    Serial.println(WiFi.RSSI());
    mdnsStarted = false;
    mqttBrokerFound = false;
  } else {
    Serial.println("[WiFi] FAILED. Kiem tra SSID/password va thu gui lai qua BLE.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
  }
}

// ============================================================
// MQTT PUBLISH STATE
// ============================================================

void publishState() {

  if (!mqttClient.connected()) {

    Serial.println(
      "MQTT NOT CONNECTED - CANNOT PUBLISH STATE"
    );

    return;
  }


  char payload[300];


  bool openLimit =
    digitalRead(LIMIT_OPEN) == LIMIT_PRESSED;

  bool closeLimit =
    digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED;


  snprintf(
    payload,
    sizeof(payload),

    "{"
      "\"node_id\":\"%s\","
      "\"device\":\"gate\","
      "\"state\":\"%s\","
      "\"limit_open\":%s,"
      "\"limit_close\":%s"
    "}",

    NODE_ID,

    gateStateToString(),

    openLimit ? "true" : "false",

    closeLimit ? "true" : "false"
  );


  bool result = mqttClient.publish(
    MQTT_STATE_TOPIC,
    payload,
    true
  );


  if (result) {

    Serial.print("MQTT STATE -> ");

    Serial.println(payload);

  }

  else {

    Serial.println(
      "MQTT STATE PUBLISH FAILED"
    );
  }
}


// ============================================================
// MQTT TELEMETRY
// ============================================================

void publishTelemetry() {

  if (!mqttClient.connected()) {
    return;
  }


  char payload[300];


  bool openLimit =
    digitalRead(LIMIT_OPEN) == LIMIT_PRESSED;

  bool closeLimit =
    digitalRead(LIMIT_CLOSE) == LIMIT_PRESSED;


  snprintf(
    payload,
    sizeof(payload),

    "{"
      "\"node_id\":\"%s\","
      "\"device\":\"gate\","
      "\"state\":\"%s\","
      "\"limit_open\":%s,"
      "\"limit_close\":%s,"
      "\"wifi_rssi\":%d"
    "}",

    NODE_ID,

    gateStateToString(),

    openLimit ? "true" : "false",

    closeLimit ? "true" : "false",

    WiFi.RSSI()
  );


  mqttClient.publish(
    MQTT_TELEMETRY_TOPIC,
    payload
  );
}


// ============================================================
// MOTOR STOP
// ============================================================

void motorStop(
  bool publishMqtt = true
) {

  digitalWrite(
    MOTOR_IN1,
    LOW
  );

  digitalWrite(
    MOTOR_IN2,
    LOW
  );


  ledcWrite(
    MOTOR_ENA,
    0
  );


  gateState = GATE_STOPPED;


  Serial.println("MOTOR STOP");


  if (publishMqtt) {

    publishState();
  }
}


// ============================================================
// MOTOR OPEN
// ============================================================

void motorOpen() {

  // ----------------------------------------------------------
  // Already open
  // ----------------------------------------------------------

  if (
    digitalRead(LIMIT_OPEN) ==
    LIMIT_PRESSED
  ) {

    Serial.println(
      "OPEN LIMIT ALREADY PRESSED"
    );


    digitalWrite(
      MOTOR_IN1,
      LOW
    );

    digitalWrite(
      MOTOR_IN2,
      LOW
    );

    ledcWrite(
      MOTOR_ENA,
      0
    );


    gateState = GATE_OPEN;


    publishState();

    return;
  }


  Serial.println(
    "OPENING GATE..."
  );


  // ----------------------------------------------------------
  // Motor direction OPEN
  // ----------------------------------------------------------

  digitalWrite(
    MOTOR_IN1,
    HIGH
  );

  digitalWrite(
    MOTOR_IN2,
    LOW
  );


  ledcWrite(
    MOTOR_ENA,
    MOTOR_SPEED
  );


  gateState = GATE_OPENING;


  publishState();
}


// ============================================================
// MOTOR CLOSE
// ============================================================

void motorClose() {

  // ----------------------------------------------------------
  // Already closed
  // ----------------------------------------------------------

  if (
    digitalRead(LIMIT_CLOSE) ==
    LIMIT_PRESSED
  ) {

    Serial.println(
      "CLOSE LIMIT ALREADY PRESSED"
    );


    digitalWrite(
      MOTOR_IN1,
      LOW
    );

    digitalWrite(
      MOTOR_IN2,
      LOW
    );

    ledcWrite(
      MOTOR_ENA,
      0
    );


    gateState = GATE_CLOSED;


    publishState();

    return;
  }


  Serial.println(
    "CLOSING GATE..."
  );


  // ----------------------------------------------------------
  // Motor direction CLOSE
  // ----------------------------------------------------------

  digitalWrite(
    MOTOR_IN1,
    LOW
  );

  digitalWrite(
    MOTOR_IN2,
    HIGH
  );


  ledcWrite(
    MOTOR_ENA,
    MOTOR_SPEED
  );


  gateState = GATE_CLOSING;


  publishState();
}


// ============================================================
// CHECK LIMIT SWITCH
// ============================================================

void checkLimitSwitch() {

  bool openPressed =
    digitalRead(LIMIT_OPEN) ==
    LIMIT_PRESSED;


  bool closePressed =
    digitalRead(LIMIT_CLOSE) ==
    LIMIT_PRESSED;


  // ==========================================================
  // OPENING
  // ==========================================================

  if (
    gateState ==
    GATE_OPENING
  ) {

    if (openPressed) {

      Serial.println();
      Serial.println(
        "=============================="
      );

      Serial.println(
        "OPEN LIMIT HIT"
      );

      Serial.println(
        "GATE FULLY OPEN"
      );

      Serial.println(
        "=============================="
      );


      digitalWrite(
        MOTOR_IN1,
        LOW
      );

      digitalWrite(
        MOTOR_IN2,
        LOW
      );

      ledcWrite(
        MOTOR_ENA,
        0
      );


      gateState = GATE_OPEN;


      publishState();
    }
  }


  // ==========================================================
  // CLOSING
  // ==========================================================

  if (
    gateState ==
    GATE_CLOSING
  ) {

    if (closePressed) {

      Serial.println();
      Serial.println(
        "=============================="
      );

      Serial.println(
        "CLOSE LIMIT HIT"
      );

      Serial.println(
        "GATE FULLY CLOSED"
      );

      Serial.println(
        "=============================="
      );


      digitalWrite(
        MOTOR_IN1,
        LOW
      );

      digitalWrite(
        MOTOR_IN2,
        LOW
      );

      ledcWrite(
        MOTOR_ENA,
        0
      );


      gateState = GATE_CLOSED;


      publishState();
    }
  }
}


// ============================================================
// PRINT LIMIT STATUS
// ============================================================

void printLimitStatus() {

  Serial.print(
    "OPEN LIMIT: "
  );


  if (
    digitalRead(LIMIT_OPEN) ==
    LIMIT_PRESSED
  ) {

    Serial.print(
      "PRESSED"
    );

  }

  else {

    Serial.print(
      "NOT PRESSED"
    );
  }


  Serial.print(
    " | CLOSE LIMIT: "
  );


  if (
    digitalRead(LIMIT_CLOSE) ==
    LIMIT_PRESSED
  ) {

    Serial.println(
      "PRESSED"
    );

  }

  else {

    Serial.println(
      "NOT PRESSED"
    );
  }
}


// ============================================================
// PRINT GATE STATUS
// ============================================================

void printGateStatus() {

  Serial.print(
    "GATE STATE: "
  );

  Serial.println(
    gateStateToString()
  );


  printLimitStatus();
}


// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {

  Serial.println();
  Serial.println(
    "======================================"
  );

  Serial.println(
    "MQTT MESSAGE RECEIVED"
  );


  Serial.print(
    "TOPIC: "
  );

  Serial.println(topic);


  // ----------------------------------------------------------
  // Convert payload to String
  // ----------------------------------------------------------

  String message;


  for (
    unsigned int i = 0;
    i < length;
    i++
  ) {

    message +=
      (char)payload[i];
  }


  message.trim();


  Serial.print(
    "PAYLOAD: "
  );

  Serial.println(message);


  // ==========================================================
  // COMMAND
  // ==========================================================

  if (
    String(topic) ==
    MQTT_COMMAND_TOPIC
  ) {


    // --------------------------------------------------------
    // OPEN
    // --------------------------------------------------------

    if (
      message.indexOf("\"action\":\"OPEN\"") >= 0 ||
      message.indexOf("\"action\":\"POWER_OFF\"") >= 0 ||
      message.indexOf("\"action\":\"UNLOCK\"") >= 0 ||
      message == "OPEN"
    ) {

      Serial.println(
        "MQTT COMMAND = OPEN"
      );

      motorOpen();
    }


    // --------------------------------------------------------
    // CLOSE
    // --------------------------------------------------------

    else if (
      message.indexOf("\"action\":\"CLOSE\"") >= 0 ||
      message.indexOf("\"action\":\"POWER_ON\"") >= 0 ||
      message.indexOf("\"action\":\"LOCK\"") >= 0 ||
      message == "CLOSE"
    ) {

      Serial.println(
        "MQTT COMMAND = CLOSE"
      );

      motorClose();
    }


    // --------------------------------------------------------
    // STOP
    // --------------------------------------------------------

    else if (
      message.indexOf("\"action\":\"STOP\"") >= 0 ||
      message == "STOP"
    ) {

      Serial.println(
        "MQTT COMMAND = STOP"
      );

      motorStop();
    }


    // --------------------------------------------------------
    // Unknown
    // --------------------------------------------------------

    else {

      Serial.println(
        "UNKNOWN MQTT COMMAND"
      );
    }
  }


  Serial.println(
    "======================================"
  );
}


// ============================================================
// MQTT CONNECT
// ============================================================

bool connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) return false;

  if (!mqttBrokerFound && !discoverMQTT()) return false;

  mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);

  Serial.println();
  Serial.println("CONNECTING TO MQTT...");
  Serial.print("Broker: ");
  Serial.print(mqttBrokerIP);
  Serial.print(":");
  Serial.println(mqttBrokerPort);


  String clientId =
    String(NODE_ID) +
    "-" +
    String((uint32_t)ESP.getEfuseMac(), HEX);


  Serial.print(
    "CLIENT ID: "
  );

  Serial.println(clientId);


  bool connected =
    mqttClient.connect(
      clientId.c_str()
    );


  if (connected) {

    Serial.println(
      "MQTT CONNECTED"
    );


    // --------------------------------------------------------
    // Subscribe command
    // --------------------------------------------------------

    bool subscribed =
      mqttClient.subscribe(
        MQTT_COMMAND_TOPIC
      );


    if (subscribed) {

      Serial.println(
        "MQTT SUBSCRIBED:"
      );

      Serial.println(
        MQTT_COMMAND_TOPIC
      );

    }

    else {

      Serial.println(
        "MQTT SUBSCRIBE FAILED"
      );
    }


    // --------------------------------------------------------
    // Publish initial state
    // --------------------------------------------------------

    publishState();


    return true;
  }


  Serial.print(
    "MQTT CONNECTION FAILED, STATE="
  );

  Serial.println(
    mqttClient.state()
  );


  return false;
}


// ============================================================
// MQTT RECONNECT
// ============================================================

void handleMQTT() {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    return;
  }


  if (
    mqttClient.connected()
  ) {

    mqttClient.loop();

    return;
  }


  unsigned long now =
    millis();


  if (
    now -
    lastMqttReconnectAttempt <
    MQTT_RECONNECT_INTERVAL
  ) {

    return;
  }


  lastMqttReconnectAttempt =
    now;


  connectMQTT();
}


// ============================================================
// SERIAL COMMAND
// ============================================================

void handleSerialCommand() {

  if (!Serial.available()) {

    return;
  }


  String command =
    Serial.readStringUntil(
      '\n'
    );


  command.trim();

  command.toUpperCase();


  // ----------------------------------------------------------
  // OPEN
  // ----------------------------------------------------------

  if (
    command == "OPEN"
  ) {

    motorOpen();
  }


  // ----------------------------------------------------------
  // CLOSE
  // ----------------------------------------------------------

  else if (
    command == "CLOSE"
  ) {

    motorClose();
  }


  // ----------------------------------------------------------
  // STOP
  // ----------------------------------------------------------

  else if (
    command == "STOP"
  ) {

    motorStop();
  }


  // ----------------------------------------------------------
  // STATUS
  // ----------------------------------------------------------

  else if (
    command == "STATUS"
  ) {

    printGateStatus();

    publishState();
  }


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  else if (
    command == "MQTT"
  ) {

    connectMQTT();
  }


  // ----------------------------------------------------------
  // Unknown
  // ----------------------------------------------------------

  else {

    Serial.println();

    Serial.println(
      "Unknown command."
    );

    Serial.println();

    Serial.println(
      "Available commands:"
    );

    Serial.println(
      "OPEN"
    );

    Serial.println(
      "CLOSE"
    );

    Serial.println(
      "STOP"
    );

    Serial.println(
      "STATUS"
    );

    Serial.println(
      "MQTT"
    );
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(1000);


  Serial.println();
  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    " ESP32-CAM SLIDING GATE + BLE WIFI + MQTT"
  );

  Serial.println(
    "======================================"
  );


  // ==========================================================
  // MOTOR
  // ==========================================================

  pinMode(
    MOTOR_IN1,
    OUTPUT
  );

  pinMode(
    MOTOR_IN2,
    OUTPUT
  );


  // ==========================================================
  // LIMIT SWITCH
  // ==========================================================

  pinMode(
    LIMIT_OPEN,
    INPUT_PULLUP
  );

  pinMode(
    LIMIT_CLOSE,
    INPUT_PULLUP
  );


  // ==========================================================
  // PWM
  // ==========================================================

  if (
    !ledcAttach(
      MOTOR_ENA,
      1000,
      8
    )
  ) {

    Serial.println(
      "ERROR: LEDC ATTACH FAILED"
    );

  }

  else {

    Serial.println(
      "LEDC PWM ATTACHED"
    );
  }


  // ==========================================================
  // MOTOR STOP
  // ==========================================================

  digitalWrite(
    MOTOR_IN1,
    LOW
  );

  digitalWrite(
    MOTOR_IN2,
    LOW
  );

  ledcWrite(
    MOTOR_ENA,
    0
  );


  gateState =
    GATE_STOPPED;


  // ==========================================================
  // MQTT
  // ==========================================================

  mqttClient.setCallback(mqttCallback);

  // ==========================================================
  // BLE WIFI PROVISIONING
  // ==========================================================

  startBLE();
  Serial.println("[SYSTEM] Scan BLE device SYNA-GATE-CAM and send WiFi credentials.");

  // Đọc thông tin WiFi và MQTT từ Flash NVS
  loadStoredWiFi();
  loadStoredMQTT();

  // Tự động kết nối nếu đã lưu WiFi trong Flash
  if (wifiSSID.length() > 0) {
    Serial.println();
    Serial.println("[NVS] Tim thay thong tin WiFi trong flash. Dang tu dong ket noi...");
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      delay(1000);
      if (discoverMQTT()) {
        connectMQTT();
      }
    } else {
      Serial.println("[WiFi] Khong the ket noi voi WiFi da luu. Dang cho BLE tu App...");
    }
  } else {
    Serial.println();
    Serial.println("[NVS] Chua co thong tin WiFi. Dang cho BLE tu App...");
  }


  // ==========================================================
  // INITIAL STATUS
  // ==========================================================

  Serial.println();

  Serial.println(
    "PIN CONFIGURATION"
  );


  Serial.print(
    "IN1         : GPIO "
  );

  Serial.println(
    MOTOR_IN1
  );


  Serial.print(
    "IN2         : GPIO "
  );

  Serial.println(
    MOTOR_IN2
  );


  Serial.print(
    "ENA         : GPIO "
  );

  Serial.println(
    MOTOR_ENA
  );


  Serial.print(
    "LIMIT OPEN  : GPIO "
  );

  Serial.println(
    LIMIT_OPEN
  );


  Serial.print(
    "LIMIT CLOSE : GPIO "
  );

  Serial.println(
    LIMIT_CLOSE
  );


  Serial.println();

  Serial.println(
    "CURRENT LIMIT STATUS:"
  );

  printLimitStatus();


  Serial.println();

  Serial.println(
    "SYSTEM READY"
  );


  Serial.println(
    "======================================"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  handleBLEProvisioning();

  // ----------------------------------------------------------
  // WiFi reconnect (Tu dong ket noi lai neu mat WiFi)
  // ----------------------------------------------------------
  static unsigned long lastWiFiReconnectAttempt = 0;
  if (wifiSSID.length() > 0 && WiFi.status() != WL_CONNECTED && !shouldConnectWiFi) {
    unsigned long now = millis();
    if (now - lastWiFiReconnectAttempt >= 10000) {
      lastWiFiReconnectAttempt = now;
      Serial.println();
      Serial.println("[WiFi] Mat ket noi WiFi! Dang thu ket noi lai...");
      mqttClient.disconnect();
      mqttBrokerFound = false;
      WiFi.reconnect();
    }
  }

  // ----------------------------------------------------------
  // Serial
  // ----------------------------------------------------------

  handleSerialCommand();


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  handleMQTT();


  // ----------------------------------------------------------
  // Limit switch
  // ----------------------------------------------------------

  checkLimitSwitch();


  delay(10);
}
