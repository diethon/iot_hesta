#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// =================================================
// WiFi - Smartphone Hotspot
// =================================================

const char* WIFI_SSID = "Dung Home T3";
const char* WIFI_PASSWORD = "123lyvanphuc";

// =================================================
// MQTT
// =================================================

const char* MQTT_SERVER = "192.168.88.104";
const int MQTT_PORT = 1883;
// =================================================
// Sensor pins
// =================================================

#define LDR_PIN 12
#define PIR_PIN 13
#define DHT_PIN 14

#define DHT_TYPE DHT22
// =================================================
// ESP32 Node
// =================================================
const char* NODE_ID = "109441d8-b6bf-4a18-bdf8-9279b7a1548d";



// =================================================
// Device IDs
// =================================================

const char* LDR_DEVICE_ID = "57c13343-f2cd-45ce-b65d-0bd843ae95d3";
const char* PIR_DEVICE_ID = "5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2";
const char* DHT_DEVICE_ID = "0bb7dc6e-0556-413d-a507-66bc8225b4ba";

// =================================================
// MQTT Topic
// =================================================

const char* LDR_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/57c13343-f2cd-45ce-b65d-0bd843ae95d3/telemetry";

const char* PIR_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2/telemetry";

const char* DHT_TOPIC =
    "hesta/nodes/109441d8-b6bf-4a18-bdf8-9279b7a1548d/devices/0bb7dc6e-0556-413d-a507-66bc8225b4ba/telemetry";

// =================================================
// Send intervals
// =================================================

// LDR: 2 seconds
const unsigned long LDR_INTERVAL = 10000;

// PIR: 1 second
const unsigned long PIR_INTERVAL = 5000;

// DHT22: 5 seconds
const unsigned long DHT_INTERVAL = 25000;

// =================================================
// Objects
// =================================================

DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =================================================
// Timers
// =================================================

unsigned long lastLDRTime = 0;
unsigned long lastPIRTime = 0;
unsigned long lastDHTTime = 0;

// =================================================
// Connect WiFi
// =================================================

void connectWiFi() {

    Serial.println();
    Serial.println("=================================");
    Serial.println("Connecting to WiFi...");
    Serial.println("=================================");

    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    int retry = 0;

    while (WiFi.status() != WL_CONNECTED && retry < 60) {

        delay(500);

        Serial.print(".");

        retry++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {

        Serial.println("WiFi connected!");

        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());

        Serial.print("Subnet: ");
        Serial.println(WiFi.subnetMask());

        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());

    } else {

        Serial.println("WiFi connection failed!");

        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
    }
}

// =================================================
// Connect MQTT
// =================================================

void connectMQTT() {

    while (!mqttClient.connected()) {

        Serial.println();
        Serial.print("Connecting to MQTT...");

        if (mqttClient.connect(NODE_ID)) {

            Serial.println(" connected!");

        } else {

            Serial.print(" failed, state = ");
            Serial.println(mqttClient.state());

            delay(2000);
        }
    }
}

// =================================================
// Publish sensor data
// =================================================

bool publishSensor(
    const char* topic,
    const char* deviceId,
    const char* metricType,
    float value,
    const char* unit
) {

    JsonDocument doc;

    doc["nodeId"] = NODE_ID;
    doc["deviceId"] = deviceId;
    doc["metricType"] = metricType;
    doc["value"] = value;
    doc["unit"] = unit;

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

    bool success = mqttClient.publish(
        topic,
        payload
    );

    if (success) {

        Serial.println("MQTT publish: SUCCESS");

    } else {

        Serial.println("MQTT publish: FAILED");
    }

    return success;
}

// =================================================
// Read + publish LDR
// =================================================

void readAndPublishLDR() {

    int lightValue = digitalRead(LDR_PIN);

    publishSensor(
        LDR_TOPIC,
        LDR_DEVICE_ID,
        "LIGHT_SENSOR",
        lightValue,
        "boolean"
    );
}

// =================================================
// Read + publish PIR
// =================================================

void readAndPublishPIR() {

    int motionValue = digitalRead(PIR_PIN);

    publishSensor(
        PIR_TOPIC,
        PIR_DEVICE_ID,
        "MOTION_SENSOR",
        motionValue,
        "boolean"
    );
}

// =================================================
// Read + publish DHT22
// =================================================

void readAndPublishDHT() {

    float temperature = dht.readTemperature();

    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {

        Serial.println();
        Serial.println("DHT22 read error!");

        return;
    }

    // ---------------------------------------------
    // Temperature
    // ---------------------------------------------

    publishSensor(
        DHT_TOPIC,
        DHT_DEVICE_ID,
        "temperature",
        temperature,
        "C"
    );

    // ---------------------------------------------
    // Humidity
    // ---------------------------------------------

    publishSensor(
        DHT_TOPIC,
        DHT_DEVICE_ID,
        "humidity",
        humidity,
        "%"
    );
}

// =================================================
// TCP TEST
// =================================================

void testMQTTConnection() {

    Serial.println();
    Serial.println("=== TCP TEST ===");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());

    Serial.print("MQTT Server: ");
    Serial.println(MQTT_SERVER);

    Serial.print("MQTT Port: ");
    Serial.println(MQTT_PORT);

    WiFiClient testClient;

    Serial.println("Connecting TCP...");

    if (testClient.connect(MQTT_SERVER, MQTT_PORT)) {

        Serial.println("TCP CONNECTION SUCCESS!");

        testClient.stop();

    } else {

        Serial.println("TCP CONNECTION FAILED!");
    }

    Serial.println("================");
}

// =================================================
// Setup
// =================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("=================================");
    Serial.println("ESP32 Smart Home Node");
    Serial.println("=================================");

    // =================================================
    // GPIO
    // =================================================

    pinMode(LDR_PIN, INPUT);

    pinMode(PIR_PIN, INPUT);

    // =================================================
    // DHT22
    // =================================================

    dht.begin();

    // =================================================
    // WiFi
    // =================================================

    connectWiFi();

    // =================================================
    // MQTT
    // =================================================

    testMQTTConnection();

    mqttClient.setServer(
        MQTT_SERVER,
        MQTT_PORT
    );

    Serial.println();
    Serial.println("Setup completed.");
}

// =================================================
// Loop
// =================================================

void loop() {

    // =================================================
    // Check WiFi
    // =================================================

    if (WiFi.status() != WL_CONNECTED) {

        Serial.println();
        Serial.println("WiFi disconnected!");

        connectWiFi();
    }

    // =================================================
    // Check MQTT
    // =================================================

    if (!mqttClient.connected()) {

        connectMQTT();
    }

    // =================================================
    // MQTT loop
    // =================================================

    mqttClient.loop();

    // =================================================
    // Current time
    // =================================================

    unsigned long currentTime = millis();

    // =================================================
    // LDR - every 2 seconds
    // =================================================

    if (currentTime - lastLDRTime >= LDR_INTERVAL) {

        lastLDRTime = currentTime;

        readAndPublishLDR();
    }

    // =================================================
    // PIR - every 1 second
    // =================================================

    if (currentTime - lastPIRTime >= PIR_INTERVAL) {

        lastPIRTime = currentTime;

        readAndPublishPIR();
    }

    // =================================================
    // DHT22 - every 5 seconds
    // =================================================

    if (currentTime - lastDHTTime >= DHT_INTERVAL) {

        lastDHTTime = currentTime;

        readAndPublishDHT();
    }
}