#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/md.h>

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
String abortedCampaignId = "";

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

void publishOtaStatus(const String& campaignId, const String& version, const String& status, const String& err) {
  StaticJsonDocument<384> doc;
  doc["campaignId"] = campaignId;
  doc["nodeCode"] = NODE_ID;
  doc["version"] = version;
  doc["status"] = status;
  doc["error"] = err;
  String out;
  serializeJson(doc, out);
  mqttClient.publish((topicPrefix() + "/ota/status").c_str(), out.c_str(), false);
  mqttClient.loop();
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

void performHttpOtaUpdate(const String& campaignId, const String& version, const String& fileUrl, const String& expectedSha256) {
  if (abortedCampaignId == campaignId) {
    publishOtaStatus(campaignId, version, "FAILED", "Campaign aborted before start");
    return;
  }

  publishOtaStatus(campaignId, version, "DOWNLOADING", "");

  HTTPClient http;
  WiFiClient downloadClient;
  http.setTimeout(20000);
  if (!http.begin(downloadClient, fileUrl)) {
    publishOtaStatus(campaignId, version, "FAILED", "Cannot open firmware URL");
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    publishOtaStatus(campaignId, version, "FAILED", "HTTP error code " + String(httpCode));
    http.end();
    return;
  }

  int contentLen = http.getSize();
  if (contentLen <= 0) {
    publishOtaStatus(campaignId, version, "FAILED", "Invalid Content-Length");
    http.end();
    return;
  }

  if (!Update.begin((size_t)contentLen)) {
    publishOtaStatus(campaignId, version, "FAILED", "Not enough flash space for OTA");
    http.end();
    return;
  }

  mbedtls_md_context_t shaCtx;
  mbedtls_md_init(&shaCtx);
  mbedtls_md_setup(&shaCtx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&shaCtx);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t totalWritten = 0;

  while (http.connected() && (totalWritten < (size_t)contentLen)) {
    mqttClient.loop();
    if (abortedCampaignId == campaignId) {
      Update.abort();
      mbedtls_md_free(&shaCtx);
      http.end();
      publishOtaStatus(campaignId, version, "FAILED", "Aborted by Admin during download");
      return;
    }

    size_t avail = stream->available();
    if (avail > 0) {
      size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      int readLen = stream->readBytes(buf, toRead);
      if (readLen > 0) {
        mbedtls_md_update(&shaCtx, buf, readLen);
        if (Update.write(buf, readLen) != (size_t)readLen) {
          Update.abort();
          mbedtls_md_free(&shaCtx);
          http.end();
          publishOtaStatus(campaignId, version, "FAILED", "Flash write error");
          return;
        }
        totalWritten += readLen;
      }
    } else {
      delay(5);
    }
  }

  publishOtaStatus(campaignId, version, "VERIFYING", "");
  uint8_t digest[32];
  mbedtls_md_finish(&shaCtx, digest);
  mbedtls_md_free(&shaCtx);
  http.end();

  char hexDigest[65];
  for (int i = 0; i < 32; i++) {
    sprintf(&hexDigest[i * 2], "%02x", digest[i]);
  }
  hexDigest[64] = '\0';

  if (expectedSha256.length() == 64 && !expectedSha256.equalsIgnoreCase(String(hexDigest))) {
    Update.abort();
    publishOtaStatus(campaignId, version, "FAILED", "SHA-256 checksum mismatch");
    return;
  }

  if (!Update.end(true)) {
    publishOtaStatus(campaignId, version, "FAILED", "Update finalize failed");
    return;
  }

  publishOtaStatus(campaignId, version, "REBOOTING", "");
  publishOtaStatus(campaignId, version, "SUCCESS", "");
  delay(500);
  ESP.restart();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr(topic);
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    return;
  }

  if (topicStr == topicPrefix() + "/ping") {
    StaticJsonDocument<256> pong;
    pong["pingId"] = doc["pingId"] | "";
    pong["nodeCode"] = NODE_ID;
    pong["firmwareVersion"] = FIRMWARE_VERSION;
    String out;
    serializeJson(pong, out);
    mqttClient.publish((topicPrefix() + "/pong").c_str(), out.c_str(), false);
    return;
  }

  if (topicStr == topicPrefix() + "/ota/abort") {
    abortedCampaignId = doc["campaignId"] | "";
    return;
  }

  if (topicStr == topicPrefix() + "/ota/notify") {
    String campaignId = doc["campaignId"] | "";
    String version = doc["version"] | "";
    String fileUrl = doc["fileUrl"] | "";
    String sha256 = doc["checksumSha256"] | "";
    if (fileUrl.length() > 0) {
      performHttpOtaUpdate(campaignId, version, fileUrl, sha256);
    }
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
    mqttClient.subscribe((topicPrefix() + "/ping").c_str());
    mqttClient.subscribe((topicPrefix() + "/ota/notify").c_str());
    mqttClient.subscribe((topicPrefix() + "/ota/abort").c_str());
    mqttClient.publish((topicPrefix() + "/status").c_str(), "\"ONLINE\"", true);
    publishNodeRegister();
    publishRelayState("");
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
    }
  }
  delay(10);
}
