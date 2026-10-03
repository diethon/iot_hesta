
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

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

const char* WIFI_SSID     = "Dung Home T3";
const char* WIFI_PASSWORD = "123lyvanphuc";


// ============================================================
// MQTT CONFIGURATION
// ============================================================

// IP máy chạy Mosquitto
// Ví dụ máy Windows của bạn:
// 192.168.88.104
const char* MQTT_SERVER = "192.168.88.104";

const int MQTT_PORT = 1883;


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
      message.indexOf(
        "\"action\":\"OPEN\""
      ) >= 0
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
      message.indexOf(
        "\"action\":\"CLOSE\""
      ) >= 0
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
      message.indexOf(
        "\"action\":\"STOP\""
      ) >= 0
    ) {

      Serial.println(
        "MQTT COMMAND = STOP"
      );

      motorStop();
    }


    // --------------------------------------------------------
    // OPEN plain text
    // --------------------------------------------------------

    else if (
      message == "OPEN"
    ) {

      motorOpen();
    }


    // --------------------------------------------------------
    // CLOSE plain text
    // --------------------------------------------------------

    else if (
      message == "CLOSE"
    ) {

      motorClose();
    }


    // --------------------------------------------------------
    // STOP plain text
    // --------------------------------------------------------

    else if (
      message == "STOP"
    ) {

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
