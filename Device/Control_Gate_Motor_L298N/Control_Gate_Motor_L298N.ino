
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/md.h>

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
// WIFI CONFIGURATION
// ============================================================

const char* WIFI_SSID     = "Sieusao";
const char* WIFI_PASSWORD = "12345678";


// ============================================================
// MQTT CONFIGURATION
// ============================================================

// IP máy chạy Mosquitto
// Ví dụ máy Windows của bạn:
// 192.168.88.104
const char* MQTT_SERVER = "192.168.137.1";

const int MQTT_PORT = 1883;


// ============================================================
// DEVICE INFORMATION
// ============================================================

const char* NODE_ID = "8d1cdd82-b339-469e-be13-7e91070f7ae5";
const char* DEVICE_TYPE = "GATE";
const char* FIRMWARE_VERSION = "v1.0.0";

const char* MQTT_NODE_PING_TOPIC     = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/ping";
const char* MQTT_NODE_PONG_TOPIC     = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/pong";
const char* MQTT_NODE_REGISTER_TOPIC = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/register";
const char* MQTT_NODE_OTA_NOTIFY     = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/ota/notify";
const char* MQTT_NODE_OTA_ABORT      = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/ota/abort";
const char* MQTT_NODE_OTA_STATUS     = "hesta/nodes/8d1cdd82-b339-469e-be13-7e91070f7ae5/ota/status";


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

  Serial.println();
  Serial.println("======================================");
  Serial.println("CONNECTING TO WIFI");
  Serial.println("======================================");

  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);


  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
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

    Serial.println("WIFI CONNECTED");

    Serial.print("IP ADDRESS: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

  }

  else {

    Serial.println("WIFI CONNECTION FAILED");

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
// DIAGNOSTICS PING/PONG & SAFE HTTP OTA FIRMWARE HANDLER
// ============================================================

String gateAbortedCampaignId = "";

String extractJsonStringField(const String& json, const String& key) {
  String pattern = "\"" + key + "\"";
  int keyIdx = json.indexOf(pattern);
  if (keyIdx < 0) return "";
  int colonIdx = json.indexOf(':', keyIdx + pattern.length());
  if (colonIdx < 0) return "";
  int startQuote = json.indexOf('"', colonIdx + 1);
  if (startQuote < 0) return "";
  int endQuote = json.indexOf('"', startQuote + 1);
  if (endQuote < 0) return "";
  return json.substring(startQuote + 1, endQuote);
}

void publishGateNodeRegister() {
  String payload = "{\"nodeId\":\"" + String(NODE_ID) +
                   "\",\"nodeCode\":\"" + String(NODE_ID) +
                   "\",\"deviceType\":\"" + String(DEVICE_TYPE) +
                   "\",\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"}";
  mqttClient.publish(MQTT_NODE_REGISTER_TOPIC, payload.c_str(), true);
}

void publishGateOtaStatus(const String& campaignId, const String& version, const String& status, const String& err) {
  String payload = "{\"campaignId\":\"" + campaignId +
                   "\",\"nodeCode\":\"" + String(NODE_ID) +
                   "\",\"version\":\"" + version +
                   "\",\"status\":\"" + status +
                   "\",\"error\":\"" + err + "\"}";
  mqttClient.publish(MQTT_NODE_OTA_STATUS, payload.c_str(), false);
  mqttClient.loop();
}

void performGateHttpOta(const String& campaignId, const String& version, const String& fileUrl, const String& expectedSha) {
  if (gateAbortedCampaignId == campaignId) {
    publishGateOtaStatus(campaignId, version, "FAILED", "Aborted before start");
    return;
  }

  // Ensure sliding gate motor is stopped before firmware flash/reboot
  motorStop();
  publishGateOtaStatus(campaignId, version, "DOWNLOADING", "");

  HTTPClient http;
  WiFiClient downloadClient;
  http.setTimeout(20000);
  if (!http.begin(downloadClient, fileUrl)) {
    publishGateOtaStatus(campaignId, version, "FAILED", "Invalid firmware URL");
    return;
  }

  int code = http.GET();
  int contentLen = http.getSize();
  if (code != HTTP_CODE_OK || contentLen <= 0 || !Update.begin((size_t)contentLen)) {
    publishGateOtaStatus(campaignId, version, "FAILED", "HTTP/Update init error: " + String(code));
    http.end();
    return;
  }

  mbedtls_md_context_t shaCtx;
  mbedtls_md_init(&shaCtx);
  mbedtls_md_setup(&shaCtx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&shaCtx);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;

  while (http.connected() && written < (size_t)contentLen) {
    mqttClient.loop();
    if (gateAbortedCampaignId == campaignId) {
      Update.abort();
      mbedtls_md_free(&shaCtx);
      http.end();
      publishGateOtaStatus(campaignId, version, "FAILED", "Aborted by Admin");
      return;
    }
    size_t avail = stream->available();
    if (avail > 0) {
      int rd = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (rd > 0) {
        mbedtls_md_update(&shaCtx, buf, rd);
        if (Update.write(buf, rd) != (size_t)rd) {
          Update.abort();
          mbedtls_md_free(&shaCtx);
          http.end();
          publishGateOtaStatus(campaignId, version, "FAILED", "Flash write error");
          return;
        }
        written += rd;
      }
    } else {
      delay(5);
    }
  }

  publishGateOtaStatus(campaignId, version, "VERIFYING", "");
  uint8_t digest[32];
  mbedtls_md_finish(&shaCtx, digest);
  mbedtls_md_free(&shaCtx);
  http.end();

  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(&hex[i * 2], "%02x", digest[i]);
  hex[64] = '\0';

  if (expectedSha.length() == 64 && !expectedSha.equalsIgnoreCase(String(hex))) {
    Update.abort();
    publishGateOtaStatus(campaignId, version, "FAILED", "SHA-256 mismatch");
    return;
  }

  if (!Update.end(true)) {
    publishGateOtaStatus(campaignId, version, "FAILED", "Update.end failed");
    return;
  }

  publishGateOtaStatus(campaignId, version, "REBOOTING", "");
  publishGateOtaStatus(campaignId, version, "SUCCESS", "");
  delay(500);
  ESP.restart();
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

  String topicStr(topic);
  if (topicStr == MQTT_NODE_PING_TOPIC) {
    String pingId = extractJsonStringField(message, "pingId");
    String pong = "{\"pingId\":\"" + pingId + "\",\"nodeCode\":\"" + String(NODE_ID) + "\",\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"}";
    mqttClient.publish(MQTT_NODE_PONG_TOPIC, pong.c_str(), false);
    return;
  }

  if (topicStr == MQTT_NODE_OTA_ABORT) {
    gateAbortedCampaignId = extractJsonStringField(message, "campaignId");
    return;
  }

  if (topicStr == MQTT_NODE_OTA_NOTIFY) {
    String campaignId = extractJsonStringField(message, "campaignId");
    String version = extractJsonStringField(message, "version");
    String fileUrl = extractJsonStringField(message, "fileUrl");
    String sha = extractJsonStringField(message, "checksumSha256");
    if (fileUrl.length() > 0) {
      performGateHttpOta(campaignId, version, fileUrl, sha);
    }
    return;
  }


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

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    return false;
  }


  Serial.println();
  Serial.println(
    "CONNECTING TO MQTT..."
  );


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
    mqttClient.subscribe(MQTT_NODE_PING_TOPIC);
    mqttClient.subscribe(MQTT_NODE_OTA_NOTIFY);
    mqttClient.subscribe(MQTT_NODE_OTA_ABORT);
    publishGateNodeRegister();


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
    " ESP32-CAM SLIDING GATE + MQTT"
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

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );


  mqttClient.setCallback(
    mqttCallback
  );
  mqttClient.setBufferSize(1024);


  // ==========================================================
  // WIFI
  // ==========================================================

  connectWiFi();


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
