
#define RELAY_PIN 13

// Đa số relay module kích mức LOW
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);

  // Ban đầu tắt relay
  digitalWrite(RELAY_PIN, RELAY_OFF);

  Serial.println("ESP32-CAM Relay Ready!");
}

void loop() {
  // Bật relay
  Serial.println("Relay ON");
  digitalWrite(RELAY_PIN, RELAY_ON);
  delay(3000);

  // Tắt relay
  Serial.println("Relay OFF");
  digitalWrite(RELAY_PIN, RELAY_OFF);
  delay(3000);
}
