#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Preferences.h>
#include <NimBLEDevice.h>

// ============================================================
// SYNA ESP32 SMART HOME NODE
//
// Features:
// - BLE Provisioning
// - WiFi credentials stored in NVS
// - MQTT broker discovered automatically via DNS-SD / mDNS
// - Change WiFi via BLE
// - MQTT is NOT configured via BLE
// - LDR / PIR / DHT22 telemetry
// ============================================================


// ============================================================
// NODE ID
// ============================================================

#define NODE_ID "109441d8-b6bf-4a18-bdf8-9279b7a1548d"


// ============================================================
// DEVICE IDs
// ============================================================

#define LDR_DEVICE_ID \
  "57c13343-f2cd-45ce-b65d-0bd843ae95d3"

#define PIR_DEVICE_ID \
  "5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2"

#define DHT_DEVICE_ID \
  "0bb7dc6e-0556-413d-a507-66bc8225b4ba"


// ============================================================
// SENSOR PINS
// ============================================================

#define LDR_PIN 12
#define PIR_PIN 13
#define DHT_PIN 14

#define DHT_TYPE DHT22


// ============================================================
// MQTT TOPICS
// ============================================================

const char* LDR_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/57c13343-f2cd-45ce-b65d-0bd843ae95d3/telemetry";

const char* PIR_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2/telemetry";

const char* DHT_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/0bb7dc6e-0556-413d-a507-66bc8225b4ba/telemetry";

// ============================================================
// MQTT TOPICS
// ============================================================

#define MQTT_DISCOVERY_TOPIC \
  "hesta/nodes/" NODE_ID "/discovery"

#define MQTT_DISCOVERY_ACK_TOPIC \
  "hesta/nodes/" NODE_ID "/discovery/ack"

#define MQTT_STATUS_TOPIC \
  "hesta/nodes/" NODE_ID "/status"

#define MQTT_COMMAND_TOPIC \
  "hesta/nodes/" NODE_ID "/devices/+/command"


// ============================================================
// BLE CONFIGURATION (Tên thiết bị & 1 Service duy nhất)
// ============================================================

#define BLE_DEVICE_NAME        "SYNA-SENSOR"
#define BLE_SERVICE_NAME       "SYNA WiFi Provisioning"

#define BLE_SERVICE_UUID \
    "12345678-1234-1234-1234-123456789000"

#define BLE_COMM_CHAR_UUID \
    "12345678-1234-1234-1234-123456789001"


// ============================================================
// OBJECTS
// ============================================================

Preferences preferences;

WiFiClient espClient;

PubSubClient mqttClient(espClient);

DHT dht(DHT_PIN, DHT_TYPE);


// ============================================================
// STORED CONFIG
// ============================================================

String wifiSSID;
String wifiPassword;

String mqttHost;
int mqttPort = 0;
bool mqttDiscoveryComplete = false;


// ============================================================
// BLE
// ============================================================

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* commCharacteristic = nullptr;
bool bleDeviceConnected = false;


// ============================================================
// FLAGS
// ============================================================

volatile bool wifiConfigPending = false;

String pendingWifiSSID;
String pendingWifiPassword;



// ============================================================
// TIMERS
// ============================================================

unsigned long lastLDRTime = 0;
unsigned long lastPIRTime = 0;
unsigned long lastDHTTime = 0;


// ============================================================
// INTERVALS
// ============================================================

const unsigned long LDR_INTERVAL = 10000;
const unsigned long PIR_INTERVAL = 5000;
const unsigned long DHT_INTERVAL = 25000;


// ============================================================
// BLE SERVER CALLBACK
// ============================================================

class ServerCallbacks : public NimBLEServerCallbacks {

    void onConnect(
        NimBLEServer* server,
        NimBLEConnInfo& connInfo
    ) override {

        bleDeviceConnected = true;

        Serial.println();
        Serial.println("=================================");
        Serial.println("BLE CLIENT CONNECTED");
        Serial.println("=================================");
    }


    void onDisconnect(
        NimBLEServer* server,
        NimBLEConnInfo& connInfo,
        int reason
    ) override {

        bleDeviceConnected = false;

        Serial.println();
        Serial.println("BLE CLIENT DISCONNECTED");

        NimBLEDevice::startAdvertising();
    }
};


// ============================================================
// BLE CONFIG CALLBACK
// ============================================================

void sendBLEStatus(const char* status, const char* message, const char* ip = nullptr);
void sendDeviceInfo();
bool connectWiFi();
bool discoverMQTTBroker();
bool connectMQTT();
void saveWiFiConfig(const String& ssid, const String& password);
void applyPendingWiFi();

class ConfigCallbacks : public NimBLECharacteristicCallbacks {

    void onWrite(
        NimBLECharacteristic* characteristic,
        NimBLEConnInfo& connInfo
    ) override {

        std::string value = characteristic->getValue();

        if (value.empty()) {
            return;
        }

        Serial.println();
        Serial.println("=================================");
        Serial.println("BLE COMMAND RECEIVED");
        Serial.println("=================================");
        Serial.println(value.c_str());

        // ----------------------------------------------------
        // Parse JSON
        // ----------------------------------------------------
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value);

        if (error) {
            Serial.print("JSON ERROR: ");
            Serial.println(error.c_str());
            sendBLEStatus("error", "Invalid JSON");
            return;
        }

        const char* action = doc["action"] | "";

        // ====================================================
        // SET WIFI
        // ====================================================
        if (strcmp(action, "set_wifi") == 0) {
            const char* ssid = doc["ssid"] | "";
            const char* password = doc["password"] | "";

            if (strlen(ssid) == 0) {
                sendBLEStatus("wifi_failed", "SSID is required");
                return;
            }

            Serial.println();
            Serial.println("WiFi configuration received via BLE");
            Serial.print("SSID: ");
            Serial.println(ssid);

            pendingWifiSSID = String(ssid);
            pendingWifiPassword = String(password);
            wifiConfigPending = true;

            // Báo App biết đã tiếp nhận cấu hình và bắt đầu quá trình kết nối
            sendBLEStatus("connecting_wifi", "Connecting to WiFi...");
            return;
        }

        // ====================================================
        // GET INFO
        // ====================================================
        if (strcmp(action, "get_info") == 0) {
            sendDeviceInfo();
            return;
        }

        // ====================================================
        // RESET WIFI
        // ====================================================
        if (strcmp(action, "reset_wifi") == 0) {
            preferences.begin("wifi", false);
            preferences.clear();
            preferences.end();

            wifiSSID = "";
            wifiPassword = "";

            WiFi.disconnect(true);
            mqttClient.disconnect();
            mqttDiscoveryComplete = false;
            mqttHost = "";
            mqttPort = 0;

            Serial.println();
            Serial.println("WiFi configuration cleared from NVS flash");

            sendBLEStatus("wifi_reset", "WiFi configuration cleared");
            return;
        }

        // ====================================================
        // UNKNOWN ACTION
        // ====================================================
        sendBLEStatus("error", "Unknown action");
    }
};


// ============================================================
// BLE STATUS
// ============================================================

void sendBLEStatus(
    const char* status,
    const char* message,
    const char* ip
) {
    if (commCharacteristic == nullptr || !bleDeviceConnected) {
        return;
    }

    JsonDocument doc;
    doc["status"] = status;
    doc["message"] = message;
    doc["nodeId"] = NODE_ID;

    if (ip != nullptr && strlen(ip) > 0) {
        doc["ip"] = ip;
    }

    if (strcmp(status, "wifi_failed") == 0 ||
        strcmp(status, "mqtt_failed") == 0 ||
        strcmp(status, "error") == 0) {
        doc["error"] = message;
    }

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));

    commCharacteristic->setValue(buffer);
    commCharacteristic->notify();

    Serial.print("BLE STATUS: ");
    Serial.println(buffer);
}


// ============================================================
// SEND DEVICE INFO
// ============================================================

void sendDeviceInfo() {
    if (commCharacteristic == nullptr || !bleDeviceConnected) {
        return;
    }

    JsonDocument doc;
    doc["nodeId"] = NODE_ID;

    doc["wifi"]["configured"] = wifiSSID.length() > 0;
    doc["wifi"]["ssid"] = wifiSSID;
    doc["wifi"]["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi"]["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";

    doc["mqtt"]["discovered"] = mqttDiscoveryComplete;
    doc["mqtt"]["host"] = mqttHost;
    doc["mqtt"]["port"] = mqttPort;
    doc["mqtt"]["connected"] = mqttClient.connected();

    char buffer[1024];
    serializeJson(doc, buffer, sizeof(buffer));

    commCharacteristic->setValue(buffer);
    commCharacteristic->notify();

    Serial.println();
    Serial.println("DEVICE INFO:");
    Serial.println(buffer);
}


// ============================================================
// START BLE
// ============================================================

void startBLE() {

    Serial.println();
    Serial.println("=================================");
    Serial.println("STARTING BLE PROVISIONING");
    Serial.println("=================================");

    NimBLEDevice::init(BLE_DEVICE_NAME);

    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    // --------------------------------------------------------
    // DUY NHẤT 1 SERVICE & 1 CHARACTERISTIC (SYNA ECOSYSTEM)
    // --------------------------------------------------------
    NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    commCharacteristic = service->createCharacteristic(
        BLE_COMM_CHAR_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::WRITE_NR |
        NIMBLE_PROPERTY::NOTIFY
    );
    commCharacteristic->setCallbacks(new ConfigCallbacks());
    commCharacteristic->setValue("{\"status\":\"ready\",\"device\":\"" BLE_DEVICE_NAME "\",\"service\":\"" BLE_SERVICE_NAME "\"}");

    // Thêm User Description Descriptor (0x2901) mô tả tên Service cho App/BLE scanner
    NimBLEDescriptor* desc = commCharacteristic->createDescriptor(
        "2901",
        NIMBLE_PROPERTY::READ
    );
    if (desc != nullptr) {
        desc->setValue(BLE_SERVICE_NAME);
    }

    service->start();

    // --------------------------------------------------------
    // BLE Advertising
    // --------------------------------------------------------
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setName(BLE_DEVICE_NAME);
    advertising->enableScanResponse(true);
    advertising->start();

    Serial.println();
    Serial.print("BLE DEVICE NAME: ");
    Serial.println(BLE_DEVICE_NAME);
    Serial.print("BLE SERVICE NAME: ");
    Serial.println(BLE_SERVICE_NAME);
    Serial.print("SERVICE UUID: ");
    Serial.println(BLE_SERVICE_UUID);
    Serial.print("COMM CHAR UUID: ");
    Serial.println(BLE_COMM_CHAR_UUID);
    Serial.println("BLE PROVISIONING READY (1 SERVICE ONLY)");
}


// ============================================================
// LOAD WIFI CONFIG
// ============================================================

void loadWiFiConfig() {

    preferences.begin(
        "wifi",
        true
    );


    wifiSSID =
        preferences.getString(
            "ssid",
            ""
        );


    wifiPassword =
        preferences.getString(
            "password",
            ""
        );


    preferences.end();


    Serial.println();
    Serial.println("=================================");
    Serial.println("WIFI CONFIG");
    Serial.println("=================================");


    if (wifiSSID.length() == 0) {

        Serial.println(
            "WiFi is NOT configured"
        );

    } else {

        Serial.print("SSID: ");
        Serial.println(wifiSSID);

        Serial.println(
            "Password: ********"
        );
    }
}


// ============================================================
// MQTT SERVICE DISCOVERY
// ============================================================

#define SYNA_MQTT_SERVICE "_syna-mqtt"
#define SYNA_MQTT_PROTO   "_tcp"

bool discoverMQTTBroker();


// ============================================================
// SAVE WIFI CONFIG
// ============================================================

void saveWiFiConfig(
    const String& ssid,
    const String& password
) {

    preferences.begin(
        "wifi",
        false
    );


    preferences.putString(
        "ssid",
        ssid
    );


    preferences.putString(
        "password",
        password
    );


    preferences.end();


    wifiSSID = ssid;

    wifiPassword = password;


    Serial.println();
    Serial.println("WiFi configuration saved to NVS");
}



// ============================================================
// CONNECT WIFI
// ============================================================

bool connectWiFi() {

    if (wifiSSID.length() == 0) {

        Serial.println();
        Serial.println(
            "WiFi is not configured."
        );

        return false;
    }


    Serial.println();
    Serial.println("=================================");
    Serial.println("CONNECTING TO WIFI");
    Serial.println("=================================");


    Serial.print("SSID: ");
    Serial.println(wifiSSID);


    WiFi.disconnect(true);

    delay(300);


    WiFi.mode(WIFI_STA);


    WiFi.begin(
        wifiSSID.c_str(),
        wifiPassword.c_str()
    );


    int retry = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        retry < 30
    ) {
        delay(500);
        Serial.print(".");
        retry++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());

        sendBLEStatus(
            "wifi_connected",
            "WiFi connected",
            WiFi.localIP().toString().c_str()
        );

        return true;
    }

    Serial.println("WiFi connection failed!");
    sendBLEStatus(
        "wifi_failed",
        "WiFi connection failed"
    );

    return false;
}


// ============================================================
// CONNECT MQTT
// ============================================================

// ============================================================
// MQTT SERVICE DISCOVERY (DNS-SD / mDNS)
// ============================================================

bool discoverMQTTBroker() {

    mqttDiscoveryComplete = false;
    mqttHost = "";
    mqttPort = 0;

    Serial.println();
    Serial.println("=================================");
    Serial.println("MQTT SERVICE DISCOVERY");
    Serial.println("=================================");
    Serial.println("Searching: _syna-mqtt._tcp.local");

    String mdnsName = "syna-" + String(NODE_ID).substring(0, 8);

    MDNS.end();

    if (!MDNS.begin(mdnsName.c_str())) {
        Serial.println("mDNS initialization failed");
        return false;
    }

    int count = MDNS.queryService("_syna-mqtt", "_tcp");

    Serial.print("Services found: ");
    Serial.println(count);

    for (int i = 0; i < count; i++) {
        IPAddress ip = MDNS.address(i);
        uint16_t port = MDNS.port(i);

        Serial.print("Service #");
        Serial.println(i + 1);
        Serial.print("Host: ");
        Serial.println(MDNS.hostname(i));
        Serial.print("IP: ");
        Serial.println(ip);
        Serial.print("Port: ");
        Serial.println(port);

        if (ip != IPAddress(0, 0, 0, 0) && port > 0) {
            mqttHost = ip.toString();
            mqttPort = port;
            mqttDiscoveryComplete = true;

            Serial.println("SYNA MQTT BROKER DISCOVERED");
            Serial.print("Broker: ");
            Serial.print(mqttHost);
            Serial.print(":");
            Serial.println(mqttPort);

            sendBLEStatus("success", "MQTT broker discovered");
            return true;
        }
    }

    Serial.println("SYNA MQTT BROKER NOT FOUND VIA mDNS.");

    // Fallback: Su dung IP Broker cau hinh san (192.168.137.107)
    mqttHost = "192.168.137.107";
    mqttPort = 1883;
    mqttDiscoveryComplete = true;

    Serial.println(">>> FALLBACK: Su dung IP Broker cau hinh san: 192.168.137.107:1883 <<<");
    sendBLEStatus("success", "MQTT broker fallback: 192.168.137.107");
    return true;
}


bool connectMQTT() {

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!mqttDiscoveryComplete || mqttHost.length() == 0 || mqttPort <= 0) {
        Serial.println("MQTT broker has not been discovered yet");
        return false;
    }

    Serial.println();
    Serial.println("=================================");
    Serial.println("CONNECTING TO MQTT");
    Serial.println("=================================");

    Serial.print("Broker: ");
    Serial.print(mqttHost);
    Serial.print(":");
    Serial.println(mqttPort);

    mqttClient.setServer(mqttHost.c_str(), mqttPort);

    int retry = 0;

    while (!mqttClient.connected() && retry < 3) {
        Serial.print("MQTT connecting...");

        if (mqttClient.connect(NODE_ID)) {
            Serial.println(" SUCCESS");
            mqttClient.subscribe(MQTT_COMMAND_TOPIC);
            sendBLEStatus("mqtt_connected", "MQTT connected");
            return true;
        }

        Serial.print(" FAILED, state=");
        Serial.println(mqttClient.state());
        delay(1000);
        retry++;
    }

    return mqttClient.connected();
}



// ============================================================
// APPLY WIFI CONFIG
// ============================================================

void applyPendingWiFi() {

    if (!wifiConfigPending) {
        return;
    }

    wifiConfigPending = false;

    Serial.println();
    Serial.println("=================================");
    Serial.println("APPLYING NEW WIFI CONFIGURATION");
    Serial.println("=================================");
    Serial.print("Connecting to: ");
    Serial.println(pendingWifiSSID);

    // Gửi thông báo đang kết nối cho BLE client (App)
    sendBLEStatus("connecting_wifi", "Connecting to WiFi...");

    mqttClient.disconnect();
    WiFi.disconnect(true);
    delay(300);

    WiFi.mode(WIFI_STA);
    WiFi.begin(
        pendingWifiSSID.c_str(),
        pendingWifiPassword.c_str()
    );

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected successfully!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        // CHỈ LƯU VÀO FLASH (NVS) KHI KẾT NỐI THÀNH CÔNG!
        saveWiFiConfig(pendingWifiSSID, pendingWifiPassword);

        // Báo App biết đã kết nối WiFi thành công kèm IP
        sendBLEStatus(
            "wifi_connected",
            "WiFi connected successfully",
            WiFi.localIP().toString().c_str()
        );

        // Khám phá broker mDNS và kết nối MQTT
        mqttDiscoveryComplete = false;
        if (discoverMQTTBroker()) {
            if (connectMQTT()) {
                sendBLEStatus("mqtt_connected", "MQTT connected successfully");
            } else {
                sendBLEStatus("mqtt_failed", "Failed to connect to MQTT broker");
            }
        } else {
            sendBLEStatus("mqtt_failed", "MQTT broker discovery failed");
        }
    } else {
        Serial.println("WiFi connection failed!");
        sendBLEStatus(
            "wifi_failed",
            "WiFi connection failed. Check SSID or password."
        );
        WiFi.disconnect(true);
    }
}



// ============================================================
// PUBLISH SENSOR
// ============================================================

bool publishSensor(
    const char* topic,
    const char* deviceId,
    const char* metricType,
    float value,
    const char* unit
) {

    if (
        !mqttClient.connected()
    ) {

        return false;
    }


    JsonDocument doc;


    doc["nodeId"] =
        NODE_ID;

    doc["deviceId"] =
        deviceId;

    doc["metricType"] =
        metricType;

    doc["value"] =
        value;

    doc["unit"] =
        unit;


    char payload[256];


    serializeJson(
        doc,
        payload,
        sizeof(payload)
    );


    Serial.println();
    Serial.println("---------------------------------");


    Serial.print("Topic: ");
    Serial.println(topic);


    Serial.print("Payload: ");
    Serial.println(payload);


    bool success =
        mqttClient.publish(
            topic,
            payload
        );


    if (success) {

        Serial.println(
            "MQTT publish: SUCCESS"
        );

    } else {

        Serial.println(
            "MQTT publish: FAILED"
        );
    }


    return success;
}


// ============================================================
// LDR
// ============================================================

void readAndPublishLDR() {

    int lightValue =
        digitalRead(LDR_PIN);


    publishSensor(
        LDR_TOPIC,
        LDR_DEVICE_ID,
        "LIGHT_SENSOR",
        lightValue,
        "boolean"
    );
}


// ============================================================
// PIR
// ============================================================

void readAndPublishPIR() {

    int motionValue =
        digitalRead(PIR_PIN);


    publishSensor(
        PIR_TOPIC,
        PIR_DEVICE_ID,
        "MOTION_SENSOR",
        motionValue,
        "boolean"
    );
}


// ============================================================
// DHT22
// ============================================================

void readAndPublishDHT() {

    float temperature =
        dht.readTemperature();


    float humidity =
        dht.readHumidity();


    if (
        isnan(temperature) ||
        isnan(humidity)
    ) {

        Serial.println(
            "DHT22 read error!"
        );

        return;
    }


    publishSensor(
        DHT_TOPIC,
        DHT_DEVICE_ID,
        "temperature",
        temperature,
        "C"
    );


    publishSensor(
        DHT_TOPIC,
        DHT_DEVICE_ID,
        "humidity",
        humidity,
        "%"
    );
}


// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(1000);


    Serial.println();
    Serial.println("=================================");
    Serial.println("SYNA SMART HOME NODE");
    Serial.println("=================================");


    Serial.print("Node ID: ");
    Serial.println(NODE_ID);


    // --------------------------------------------------------
    // GPIO
    // --------------------------------------------------------

    pinMode(
        LDR_PIN,
        INPUT
    );


    pinMode(
        PIR_PIN,
        INPUT
    );


    // --------------------------------------------------------
    // DHT
    // --------------------------------------------------------

    dht.begin();


    // --------------------------------------------------------
    // Start BLE Provisioning Service
    // Bật BLE trước để App luôn quét thấy thiết bị ngay khi bật nguồn
    // --------------------------------------------------------
    startBLE();

    // --------------------------------------------------------
    // Load configuration from Flash NVS
    // --------------------------------------------------------
    loadWiFiConfig();

    // --------------------------------------------------------
    // Connect WiFi (Tự động kết nối nếu đã lưu cấu hình trong Flash)
    // --------------------------------------------------------
    if (wifiSSID.length() > 0) {
        Serial.println();
        Serial.println("[NVS] Tim thay thong tin WiFi trong flash. Dang tu dong ket noi...");

        if (connectWiFi()) {
            if (discoverMQTTBroker()) {
                connectMQTT();
            }
        } else {
            Serial.println();
            Serial.println("[WiFi] Khong the ket noi voi WiFi da luu. Dang cho cau hinh BLE tu App...");
        }

    } else {
        Serial.println();
        Serial.println("[NVS] Chua co thong tin WiFi. Dang cho cau hinh qua BLE tu App...");
    }

    Serial.println();
    Serial.println("=================================");
    Serial.println("SETUP COMPLETED");
    Serial.println("=================================");
}


// ============================================================
// LOOP
// ============================================================

void loop() {

    // --------------------------------------------------------
    // BLE provisioning requests
    // --------------------------------------------------------
    applyPendingWiFi();

    // --------------------------------------------------------
    // WiFi reconnect (Non-blocking timer)
    // --------------------------------------------------------
    static unsigned long lastWiFiReconnect = 0;
    if (wifiSSID.length() > 0 && WiFi.status() != WL_CONNECTED && !wifiConfigPending) {
        unsigned long now = millis();
        if (now - lastWiFiReconnect >= 10000) {
            lastWiFiReconnect = now;
            Serial.println();
            Serial.println("[WiFi] Mat ket noi WiFi! Dang thu ket noi lai...");

            mqttClient.disconnect();
            mqttDiscoveryComplete = false;
            mqttHost = "";
            mqttPort = 0;

            WiFi.reconnect();
        }
    }


    // --------------------------------------------------------
    // MQTT discovery / reconnect
    // --------------------------------------------------------

    static unsigned long lastDiscoveryAttempt = 0;
    static unsigned long lastMqttReconnect = 0;

    if (WiFi.status() == WL_CONNECTED) {

        if (!mqttDiscoveryComplete &&
            millis() - lastDiscoveryAttempt >= 5000) {

            lastDiscoveryAttempt = millis();

            if (discoverMQTTBroker()) {
                connectMQTT();
            }
        }

        if (mqttDiscoveryComplete &&
            !mqttClient.connected() &&
            millis() - lastMqttReconnect >= 5000) {

            lastMqttReconnect = millis();
            connectMQTT();
        }
    }


    // --------------------------------------------------------
    // MQTT loop
    // --------------------------------------------------------

    mqttClient.loop();


    // --------------------------------------------------------
    // Current time
    // --------------------------------------------------------

    unsigned long currentTime =
        millis();


    // --------------------------------------------------------
    // LDR
    // --------------------------------------------------------

    if (
        currentTime - lastLDRTime
        >= LDR_INTERVAL
    ) {

        lastLDRTime =
            currentTime;


        readAndPublishLDR();
    }


    // --------------------------------------------------------
    // PIR
    // --------------------------------------------------------

    if (
        currentTime - lastPIRTime
        >= PIR_INTERVAL
    ) {

        lastPIRTime =
            currentTime;


        readAndPublishPIR();
    }


    // --------------------------------------------------------
    // DHT22
    // --------------------------------------------------------

    if (
        currentTime - lastDHTTime
        >= DHT_INTERVAL
    ) {

        lastDHTTime =
            currentTime;


        readAndPublishDHT();
    }


    delay(10);
}