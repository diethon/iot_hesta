#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "HestaOta.h"

// ============================================================
// HESTA SMART HOME - ESP32 RELAY & OTA-READY CONTROLLER
// Supports:
//  - Relay control (TURN_ON / TURN_OFF / SET_POWER) + ACK
//  - Node Registration with firmwareVersion & deviceType
//  - Real Diagnostics Ping/Pong RTT measurement
//  - Safe HTTP OTA Update with SHA-256 verification + Abort
// ============================================================

#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

#define MQTT_SERVER      "192.168.88.120"
#define MQTT_PORT        1883
#define MQTT_USER        ""
#define MQTT_PASSWORD    ""

#define NODE_ID          "ESP32-RELAY-01"
#define DEVICE_ID        "00000000-0000-0000-0000-000000000001"
#define DEVICE_TYPE      "ESP32_RELAY"
#define FIRMWARE_VERSION "v1.0.0"

#define RELAY_PIN        2
#define RELAY_ACTIVE     HIGH
#define RELAY_INACTIVE   LOW

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool relayOn = false;

String topicPrefix() {
  return String("hesta/nodes/") + NODE_ID;
}

void publishNodeRegister() {
  StaticJsonDocument<256> doc;
  doc["nodeId"] = NODE_ID;
  doc["nodeCode"] = NODE_ID;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  String out;
  serializeJson(doc, out);
  mqttClient.publish((topicPrefix() + "/register").c_str(), out.c_str(), true);
}

void publishRelayState(const char* commandId) {
  StaticJsonDocument<384> doc;
  doc["deviceId"] = DEVICE_ID;
  doc["power"] = relayOn ? "ON" : "OFF";
  String stateOut;
  serializeJson(doc, stateOut);
  mqttClient.publish((topicPrefix() + "/devices/" + DEVICE_ID + "/state").c_str(), stateOut.c_str(), false);

  if (commandId && strlen(commandId) > 0) {
    StaticJsonDocument<384> ack;
    ack["commandId"] = commandId;
    ack["deviceId"] = DEVICE_ID;
    ack["status"] = "SUCCESS";
    JsonObject st = ack.createNestedObject("state");
    st["power"] = relayOn ? "ON" : "OFF";
    String ackOut;
    serializeJson(ack, ackOut);
    mqttClient.publish((topicPrefix() + "/devices/" + DEVICE_ID + "/ack").c_str(), ackOut.c_str(), false);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (hestaOtaHandleMqttMessage(topic, payload, length)) {
    return;
  }

  String topicStr(topic);
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    return;
  }

  if (topicStr == topicPrefix() + "/devices/" + DEVICE_ID + "/command") {
    String action = doc["action"] | "";
    action.toUpperCase();
    const char* commandId = doc["commandId"] | "";
    if (action == "TURN_ON" || action == "POWER_ON") {
      relayOn = true;
    } else if (action == "TURN_OFF" || action == "POWER_OFF") {
      relayOn = false;
    } else if (action == "SET_POWER") {
      bool p = doc["parameters"]["power"] | doc["power"] | relayOn;
      relayOn = p;
    }
    digitalWrite(RELAY_PIN, relayOn ? RELAY_ACTIVE : RELAY_INACTIVE);
    publishRelayState(commandId);
  }
}

void ensureMqttConnected() {
  if (mqttClient.connected()) return;
  String clientId = String(NODE_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = (strlen(MQTT_USER) > 0)
      ? mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)
      : mqttClient.connect(clientId.c_str());
  if (ok) {
    mqttClient.subscribe((topicPrefix() + "/devices/+/command").c_str());
    mqttClient.publish((topicPrefix() + "/status").c_str(), "\"ONLINE\"", true);
    publishNodeRegister();
    publishRelayState("");
    hestaOtaOnMqttConnected();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_INACTIVE);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(1024);
  mqttClient.setCallback(mqttCallback);
  hestaOtaInit(&mqttClient, NODE_ID, FIRMWARE_VERSION);
  ensureMqttConnected();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      static unsigned long lastTry = 0;
      if (millis() - lastTry > 5000) {
        lastTry = millis();
        ensureMqttConnected();
      }
    } else {
      mqttClient.loop();
      hestaOtaLoop();
    }
  }
  delay(10);
}
