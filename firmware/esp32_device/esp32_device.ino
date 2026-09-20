#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "BROKER_IP";

WiFiClient espClient;
PubSubClient client(espClient);

const int RELAY_PIN = 2;
String nodeId = "node_01";
String deviceId = "light_01";

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  
  StaticJsonDocument<200> doc;
  deserializeJson(doc, messageTemp);
  String action = doc["action"];
  String commandId = doc["commandId"];
  
  if(action == "TURN_ON"){
    digitalWrite(RELAY_PIN, HIGH);
  } else if(action == "TURN_OFF"){
    digitalWrite(RELAY_PIN, LOW);
  }
  
  // Publish ACK
  String ackTopic = "hesta/nodes/" + nodeId + "/devices/" + deviceId + "/ack";
  StaticJsonDocument<200> ackDoc;
  ackDoc["commandId"] = commandId;
  ackDoc["deviceId"] = deviceId;
  ackDoc["status"] = "SUCCESS";
  char ackBuffer[200];
  serializeJson(ackDoc, ackBuffer);
  client.publish(ackTopic.c_str(), ackBuffer);
  
  // Publish State
  String stateTopic = "hesta/nodes/" + nodeId + "/devices/" + deviceId + "/state";
  StaticJsonDocument<200> stateDoc;
  stateDoc["deviceId"] = deviceId;
  JsonObject stateObj = stateDoc.createNestedObject("state");
  stateObj["power"] = action == "TURN_ON" ? "ON" : "OFF";
  char stateBuffer[200];
  serializeJson(stateDoc, stateBuffer);
  client.publish(stateTopic.c_str(), stateBuffer, true);
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client")) {
      String commandTopic = "hesta/nodes/" + nodeId + "/devices/+/command";
      client.subscribe(commandTopic.c_str());
      
      // Heartbeat Online
      String statusTopic = "hesta/nodes/" + nodeId + "/status";
      client.publish(statusTopic.c_str(), "{\"status\":\"ONLINE\"}", true);
    } else {
      delay(5000);
    }
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Optional: Send heartbeat every X seconds
}
