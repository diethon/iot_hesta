/*
 * HESTA Smart Home - ESP32 Fall Alarm & mmWave Radar Hub
 * ======================================================
 * Hardware:
 * - ESP32-WROOM-32 / ESP32-C3
 * - Module Relay 5V có cách ly quang (Optocoupler) trên GPIO 23
 * - Còi hú báo động 5V / 12V DC
 * - Cảm biến mmWave Radar 60GHz (HLK-LD6002 / MR60FDA1) trên Serial2 (GPIO 16 RX2, GPIO 17 TX2)
 *
 * MQTT Topics:
 * - Subscribes: hesta/actuators/siren
 * - Subscribes: hesta/sensor/fall-detection/+/event
 * - Publishes:  hesta/sensor/radar/bathroom/telemetry
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==========================================
// CẤU HÌNH WIFI & MQTT (Tùy chỉnh theo mạng nhà)
// ==========================================
const char* ssid        = "HESTA_SmartHome_2.4G";
const char* password    = "Hesta@2026";
const char* mqtt_server = "192.168.1.100"; // IP của Orange Pi 5 Plus
const int   mqtt_port   = 1883;
const char* client_id   = "hesta_esp32_alarm_node_01";

// ==========================================
// ĐỊNH NGHĨA CHÂN PINOUT
// ==========================================
#define PIN_RELAY_SIREN 23 // Chân kích Relay Optocoupler điều khiển còi 12V
#define PIN_STATUS_LED  2  // Đèn LED báo trạng thái bo mạch
#define RADAR_RX_PIN    16 // Serial2 RX2 nối TX của Radar mmWave 60GHz
#define RADAR_TX_PIN    17 // Serial2 TX2 nối RX của Radar mmWave 60GHz

// Relay active level (thường là LOW trigger đối với module relay opto)
#define RELAY_ACTIVE    LOW
#define RELAY_INACTIVE  HIGH

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Trạng thái còi hú & Timer an toàn
bool isSirenActive = false;
unsigned long sirenStartTime = 0;
unsigned long maxSirenDurationMs = 60000; // Tối đa 60 giây tự ngắt còi để chống ô nhiễm tiếng ồn

void setupWiFi() {
  delay(10);
  Serial.println("\n[WiFi] Đang kết nối tới: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Đã kết nối thành công!");
    Serial.print("[WiFi] Địa chỉ IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(PIN_STATUS_LED, HIGH);
  } else {
    Serial.println("\n[WiFi] Cảnh báo: Kết nối WiFi thất bại! Tiếp tục chạy offline...");
  }
}

void triggerSiren(bool enable, unsigned long durationMs = 60000) {
  if (enable) {
    Serial.println("[SIREN] >>> KÍCH HOẠT CÒI HÚ KHẨN CẤP 12V!");
    digitalWrite(PIN_RELAY_SIREN, RELAY_ACTIVE);
    isSirenActive = true;
    sirenStartTime = millis();
    maxSirenDurationMs = durationMs;
  } else {
    Serial.println("[SIREN] --- TẮT CÒI HÚ. AN TOÀN.");
    digitalWrite(PIN_RELAY_SIREN, RELAY_INACTIVE);
    isSirenActive = false;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT Callback] Nhận tin nhắn trên topic: ");
  Serial.println(topic);

  // Parse JSON Payload
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("[JSON] Lỗi parse payload: ");
    Serial.println(error.c_str());
    return;
  }

  // 1. Lệnh điều khiển còi trực tiếp từ Spring Boot / App
  if (strcmp(topic, "hesta/actuators/siren") == 0) {
    const char* command = doc["command"] | "STOP";
    int durationSec = doc["durationSec"] | 60;

    if (strcmp(command, "START") == 0 || strcmp(command, "CRITICAL_SOS") == 0) {
      triggerSiren(true, durationSec * 1000UL);
    } else if (strcmp(command, "STOP") == 0) {
      triggerSiren(false);
    }
  }

  // 2. Tự động phản ứng cục bộ khi Camera Edge AI bắn sự kiện CONFIRMED_FALL
  if (strstr(topic, "fall-detection") != NULL) {
    const char* eventType = doc["eventType"] | "NONE";
    const char* incidentClass = doc["incidentClassification"] | "NONE";

    if (strcmp(eventType, "FALL_DETECTED") == 0) {
      Serial.printf("[LOCAL FALL ALERT] Phát hiện té ngã: %s! Kích hoạt còi báo động tại chỗ...\n", incidentClass);
      triggerSiren(true, 45000); // Hú 45 giây
    }
  }
}

void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("[MQTT] Đang kết nối tới MQTT Broker tại: ");
    Serial.println(mqtt_server);

    if (mqttClient.connect(client_id)) {
      Serial.println("[MQTT] Đã kết nối thành công!");
      // Đăng ký nhận topic lệnh còi hú và sự kiện té ngã
      mqttClient.subscribe("hesta/actuators/siren");
      mqttClient.subscribe("hesta/sensor/fall-detection/+/event");
      Serial.println("[MQTT] Đã subscribe: hesta/actuators/siren & hesta/sensor/fall-detection/+/event");
    } else {
      Serial.print("[MQTT] Lỗi kết nối, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n==========================================");
  Serial.println(" HESTA ESP32 FALL ALARM & SIREN CONTROLLER");
  Serial.println("==========================================");

  // Cấu hình chân GPIO Relay & LED
  pinMode(PIN_RELAY_SIREN, OUTPUT);
  digitalWrite(PIN_RELAY_SIREN, RELAY_INACTIVE); // Mặc định tắt còi

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  // Cấu hình Serial2 cho Radar mmWave 60GHz
  Serial2.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);

  // Kết nối mạng
  setupWiFi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  // Duy trì kết nối MQTT
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      static unsigned long lastMqttRetry = 0;
      if (millis() - lastMqttRetry > 5000) {
        lastMqttRetry = millis();
        reconnectMQTT();
      }
    } else {
      mqttClient.loop();
    }
  }

  // Timer an toàn tự ngắt còi hú
  if (isSirenActive && (millis() - sirenStartTime >= maxSirenDurationMs)) {
    Serial.println("[SIREN TIMER] Hết thời gian báo động tối đa. Tự động ngắt còi!");
    triggerSiren(false);
  }

  // Đọc dữ liệu từ Radar mmWave 60GHz (nếu có tín hiệu UART)
  if (Serial2.available()) {
    String radarData = Serial2.readStringUntil('\n');
    radarData.trim();
    if (radarData.length() > 0) {
      Serial.print("[RADAR 60GHz] ");
      Serial.println(radarData);

      // Nếu radar phát hiện ngã (ví dụ bản tin HLK-LD6002 chứa cờ $FALL)
      if (radarData.indexOf("FALL") >= 0 || radarData.indexOf("$03,01") >= 0) {
        Serial.println("[RADAR ALERT] Radar mmWave phát hiện ngã trong phòng tắm!");
        triggerSiren(true, 60000);

        if (mqttClient.connected()) {
          StaticJsonDocument<256> radarDoc;
          radarDoc["deviceId"] = "radar_bathroom_01";
          radarDoc["zoneType"] = "MMWAVE_RADAR";
          radarDoc["eventType"] = "FALL_DETECTED";
          radarDoc["incidentClassification"] = "ACCIDENTAL_FALL";
          radarDoc["confidence"] = 0.98;
          radarDoc["timestamp"] = millis();

          char buffer[256];
          serializeJson(radarDoc, buffer);
          mqttClient.publish("hesta/sensor/radar/bathroom/event", buffer, 1);
        }
      }
    }
  }

  // Heartbeat nhấp nháy LED định kỳ 2 giây
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 2000) {
    lastHeartbeat = millis();
    if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
      digitalWrite(PIN_STATUS_LED, HIGH);
      delay(50);
      digitalWrite(PIN_STATUS_LED, LOW);
    }
  }
}
