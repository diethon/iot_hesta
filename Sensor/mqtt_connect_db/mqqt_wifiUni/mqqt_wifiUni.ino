#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// =================================================
// WiFi - FPT University
// =================================================

const char* WIFI_SSID = "FPT University";
const char* WIFI_USERNAME = "DE180895";
const char* WIFI_PASSWORD = "congtoan0101";

// =================================================
// MQTT
// =================================================

const char* MQTT_SERVER = "10.12.56.210";
const int MQTT_PORT = 1883;

// =================================================
// ESP32 Node
// =================================================

const char* NODE_ID = "ESP32-001";

// =================================================
// MQTT Topic
// =================================================

const char* MQTT_TOPIC = "hesta/edge/ESP32-001/telemetry";

// =================================================
// Sensor pins
// =================================================

#define LDR_PIN 12
#define PIR_PIN 13
#define DHT_PIN 14

#define DHT_TYPE DHT22

// =================================================
// Objects
// =================================================

DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =================================================
// Connect WiFi - WPA2 Enterprise
// =================================================

void connectWiFi() {

    Serial.println();
    Serial.println("=================================");
    Serial.println("Connecting to university WiFi...");
    Serial.println("=================================");

    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);

    // WPA2 Enterprise / PEAP
    WiFi.begin(
        WIFI_SSID,
        WPA2_AUTH_PEAP,
        WIFI_USERNAME,
        WIFI_USERNAME,
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

void testTCP(const char* host, uint16_t port, const char* name) {
    WiFiClient client;

    Serial.println();
    Serial.print("Testing ");
    Serial.print(name);
    Serial.print(" -> ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);

    if (client.connect(host, port)) {
        Serial.println("SUCCESS");
        client.stop();
    } else {
        Serial.println("FAILED");
    }
}
// =================================================
// Read sensors
// =================================================

void readAndPublishSensors() {

    // -------------------------------------------------
    // LDR
    // -------------------------------------------------

    int lightValue = analogRead(LDR_PIN);

    // -------------------------------------------------
    // PIR
    // -------------------------------------------------

    int motionValue = digitalRead(PIR_PIN);

    // -------------------------------------------------
    // DHT22
    // -------------------------------------------------

    float temperature = dht.readTemperature();

    float humidity = dht.readHumidity();

    // -------------------------------------------------
    // Check DHT22
    // -------------------------------------------------

    if (isnan(temperature) || isnan(humidity)) {

        Serial.println("DHT22 read error!");

        return;
    }

    // =================================================
    // Create JSON
    // =================================================

    JsonDocument doc;

    doc["nodeId"] = NODE_ID;

    JsonArray readings = doc["readings"].to<JsonArray>();

    // =================================================
    // LDR
    // =================================================

    JsonObject light = readings.add<JsonObject>();

    light["deviceId"] = "LDR-01";
    light["metricType"] = "light";
    light["value"] = lightValue;
    light["unit"] = "adc";

    // =================================================
    // PIR
    // =================================================

    JsonObject motion = readings.add<JsonObject>();

    motion["deviceId"] = "PIR-01";
    motion["metricType"] = "motion";
    motion["value"] = motionValue;
    motion["unit"] = "boolean";

    // =================================================
    // DHT22 - Temperature
    // =================================================

    JsonObject temp = readings.add<JsonObject>();

    temp["deviceId"] = "DHT-01";
    temp["metricType"] = "temperature";
    temp["value"] = temperature;
    temp["unit"] = "C";

    // =================================================
    // DHT22 - Humidity
    // =================================================

    JsonObject hum = readings.add<JsonObject>();

    hum["deviceId"] = "DHT-01";
    hum["metricType"] = "humidity";
    hum["value"] = humidity;
    hum["unit"] = "%";

    // =================================================
    // Serialize JSON
    // =================================================

    char payload[768];

    serializeJson(doc, payload, sizeof(payload));

    // =================================================
    // Serial output
    // =================================================

    Serial.println();
    Serial.println("=================================");
    Serial.println("Sensor readings");
    Serial.println("=================================");

    Serial.print("LDR: ");
    Serial.println(lightValue);

    Serial.print("PIR: ");
    Serial.println(motionValue);

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.println();
    Serial.println("MQTT Payload:");
    Serial.println(payload);

    // =================================================
    // Publish MQTT
    // =================================================

    bool success = mqttClient.publish(
        MQTT_TOPIC,
        payload
    );

    if (success) {

        Serial.println("MQTT publish: SUCCESS");

    } else {

        Serial.println("MQTT publish: FAILED");
    }
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
    testTCP("10.12.48.1", 80, "Gateway");
testTCP("10.12.56.210", 1883, "Windows MQTT");
testTCP("10.12.56.210", 9001, "Windows MQTT WebSocket");

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
    // Read + publish sensors
    // =================================================

    readAndPublishSensors();

    // =================================================
    // Wait 5 seconds
    // =================================================

    delay(5000);
}