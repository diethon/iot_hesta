
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

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

#define DEFAULT_MQTT_SERVER "192.168.88.104"
#define DEFAULT_MQTT_PORT   1883

String wifiSSID;
String wifiPassword;
String mqttServer;
int mqttPort = DEFAULT_MQTT_PORT;

Preferences preferences;


// ============================================================
// MQTT CONFIGURATION
// ============================================================



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
// BLE PROVISIONING
// ============================================================

#define BLE_SERVICE_UUID "7c4a0001-8a7e-4f9d-a1c1-123456789001"
#define BLE_CONFIG_UUID  "7c4a0002-8a7e-4f9d-a1c1-123456789002"
#define BLE_STATUS_UUID  "7c4a0003-8a7e-4f9d-a1c1-123456789003"

NimBLECharacteristic* bleConfigCharacteristic = nullptr;
NimBLECharacteristic* bleStatusCharacteristic = nullptr;

volatile bool wifiConfigPending = false;
volatile bool mqttConfigPending = false;

String pendingWifiSSID;
String pendingWifiPassword;
String pendingMqttServer;
int pendingMqttPort = DEFAULT_MQTT_PORT;


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
// FORWARD DECLARATIONS
// ============================================================

const char* gateStateToString();
void connectWiFi();
bool connectMQTT();

// ============================================================
// NVS CONFIG
// ============================================================

void loadConfiguration() {
  preferences.begin("wifi", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  preferences.begin("mqtt", true);
  mqttServer = preferences.getString("server", DEFAULT_MQTT_SERVER);
  mqttPort = preferences.getInt("port", DEFAULT_MQTT_PORT);
  preferences.end();

  Serial.println("\n=== NVS CONFIG ===");
  Serial.print("WiFi SSID: ");
  Serial.println(wifiSSID.length() ? wifiSSID : "(not configured)");
  Serial.print("MQTT: ");
  Serial.print(mqttServer);
  Serial.print(":");
  Serial.println(mqttPort);
}

void saveWiFiConfiguration(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();

  wifiSSID = ssid;
  wifiPassword = password;
}

void saveMQTTConfiguration(const String& server, int port) {
  preferences.begin("mqtt", false);
  preferences.putString("server", server);
  preferences.putInt("port", port);
  preferences.end();

  mqttServer = server;
  mqttPort = port;
}

// ============================================================
// BLE STATUS
// ============================================================

void sendBLEStatus(const char* status, const char* message) {
  if (!bleStatusCharacteristic) return;

  JsonDocument doc;
  doc["status"] = status;
  doc["message"] = message;
  doc["nodeId"] = NODE_ID;
  doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
  doc["mqtt"]["server"] = mqttServer;
  doc["mqtt"]["port"] = mqttPort;
  doc["mqtt"]["connected"] = mqttClient.connected();

  char buffer[768];
  serializeJson(doc, buffer, sizeof(buffer));

  bleStatusCharacteristic->setValue(buffer);
  bleStatusCharacteristic->notify();

  Serial.print("BLE STATUS: ");
  Serial.println(buffer);
}

void sendBLEInfo() {
  if (!bleStatusCharacteristic) return;

  JsonDocument doc;
  doc["nodeId"] = NODE_ID;

  doc["wifi"]["configured"] = wifiSSID.length() > 0;
  doc["wifi"]["ssid"] = wifiSSID;
  doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;

  doc["mqtt"]["server"] = mqttServer;
  doc["mqtt"]["port"] = mqttPort;
  doc["mqtt"]["connected"] = mqttClient.connected();

  doc["gate"]["state"] = gateStateToString();

  char buffer[768];
  serializeJson(doc, buffer, sizeof(buffer));

  bleStatusCharacteristic->setValue(buffer);
  bleStatusCharacteristic->notify();

  Serial.print("BLE INFO: ");
  Serial.println(buffer);
}

// ============================================================
// BLE CALLBACKS
// ============================================================

class BLEServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    Serial.println("BLE CLIENT CONNECTED");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("BLE CLIENT DISCONNECTED");
    NimBLEDevice::startAdvertising();
  }
};

class BLEConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    std::string value = characteristic->getValue();

    if (value.empty()) return;

    Serial.println("\n=== BLE CONFIG ===");
    Serial.println(value.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, value);

    if (error) {
      sendBLEStatus("error", "Invalid JSON");
      return;
    }

    const char* action = doc["action"] | "";

    if (strcmp(action, "set_wifi") == 0) {
      const char* ssid = doc["ssid"] | "";
      const char* password = doc["password"] | "";

      if (strlen(ssid) == 0) {
        sendBLEStatus("error", "SSID is empty");
        return;
      }

      pendingWifiSSID = ssid;
      pendingWifiPassword = password;
      wifiConfigPending = true;

      sendBLEStatus("processing", "WiFi configuration received");
      return;
    }

    if (strcmp(action, "set_mqtt") == 0) {
      const char* host = doc["host"] | "";
      int port = doc["port"] | DEFAULT_MQTT_PORT;

      if (strlen(host) == 0) {
        sendBLEStatus("error", "MQTT host is empty");
        return;
      }

      if (port < 1 || port > 65535) {
        sendBLEStatus("error", "Invalid MQTT port");
        return;
      }

      pendingMqttServer = host;
      pendingMqttPort = port;
      mqttConfigPending = true;

      sendBLEStatus("processing", "MQTT configuration received");
      return;
    }

    if (strcmp(action, "get_info") == 0) {
      sendBLEInfo();
      return;
    }

    if (strcmp(action, "reset_wifi") == 0) {
      preferences.begin("wifi", false);
      preferences.clear();
      preferences.end();

      wifiSSID = "";
      wifiPassword = "";
      mqttClient.disconnect();

      sendBLEStatus("success", "WiFi configuration removed");
      return;
    }

    if (strcmp(action, "reset_mqtt") == 0) {
      preferences.begin("mqtt", false);
      preferences.clear();
      preferences.end();

      mqttServer = DEFAULT_MQTT_SERVER;
      mqttPort = DEFAULT_MQTT_PORT;

      mqttClient.disconnect();
      mqttClient.setServer(mqttServer.c_str(), mqttPort);

      sendBLEStatus("success", "MQTT configuration reset");
      return;
    }

    sendBLEStatus("error", "Unknown action");
  }
};

// ============================================================
// START BLE
// ============================================================

void startBLE() {
  String bleName = "SYNA-GATE-" + String(NODE_ID).substring(0, 8);

  NimBLEDevice::init(bleName.c_str());

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new BLEServerCallbacks());

  NimBLEService* service =
    server->createService(BLE_SERVICE_UUID);

  bleConfigCharacteristic =
    service->createCharacteristic(
      BLE_CONFIG_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );

  bleConfigCharacteristic->setCallbacks(
    new BLEConfigCallbacks()
  );

  bleStatusCharacteristic =
    service->createCharacteristic(
      BLE_STATUS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

  bleStatusCharacteristic->setValue("SYNA GATE READY");

  service->start();

  NimBLEAdvertising* advertising =
    NimBLEDevice::getAdvertising();

  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setName(bleName.c_str());
  advertising->start();

  Serial.println("\n=== BLE PROVISIONING READY ===");
  Serial.print("BLE NAME: ");
  Serial.println(bleName);
}

// ============================================================
// APPLY BLE CONFIG
// ============================================================

void applyPendingWiFi() {
  if (!wifiConfigPending) return;

  wifiConfigPending = false;

  saveWiFiConfiguration(
    pendingWifiSSID,
    pendingWifiPassword
  );

  mqttClient.disconnect();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    mqttClient.setServer(
      mqttServer.c_str(),
      mqttPort
    );
    connectMQTT();
  }
}

void applyPendingMQTT() {
  if (!mqttConfigPending) return;

  mqttConfigPending = false;

  saveMQTTConfiguration(
    pendingMqttServer,
    pendingMqttPort
  );

  mqttClient.disconnect();

  mqttClient.setServer(
    mqttServer.c_str(),
    mqttPort
  );

  Serial.print("NEW MQTT SERVER: ");
  Serial.println(mqttServer);

  Serial.print("NEW MQTT PORT: ");
  Serial.println(mqttPort);

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
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

  if (wifiSSID.length() == 0) {
    Serial.println("WIFI NOT CONFIGURED - waiting for BLE");
    return;
  }

  Serial.println("\n======================================");
  Serial.println("CONNECTING TO WIFI");
  Serial.println("======================================");

  Serial.print("SSID: ");
  Serial.println(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

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
    Serial.println("WIFI CONNECTED");
    Serial.print("IP ADDRESS: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

    sendBLEStatus("success", "WiFi connected");
  } else {
    Serial.println("WIFI CONNECTION FAILED");
    sendBLEStatus("error", "WiFi connection failed");
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


  mqttClient.setServer(
    mqttServer.c_str(),
    mqttPort
  );

  Serial.print("MQTT SERVER: ");
  Serial.println(mqttServer);

  Serial.print("MQTT PORT: ");
  Serial.println(mqttPort);

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


  // ==========================================================
  // LOAD NVS CONFIGURATION
  // ==========================================================

  loadConfiguration();

  // ==========================================================
  // BLE PROVISIONING
  // ==========================================================

  startBLE();

  // ==========================================================
  // WIFI
  // ==========================================================

  connectWiFi();

  mqttClient.setServer(
    mqttServer.c_str(),
    mqttPort
  );


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
  // BLE provisioning
  // ----------------------------------------------------------

  applyPendingWiFi();
  applyPendingMQTT();

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
