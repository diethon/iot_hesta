#include <ESP32Servo.h>

Servo mg996r;

#define SERVO_PIN 14

void setup() {
  Serial.begin(115200);

  // Cho phép ESP32Servo sử dụng timer
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  mg996r.setPeriodHertz(50);      // MG996R: 50Hz
  mg996r.attach(SERVO_PIN, 500, 2400);

  Serial.println("MG996R TEST");

  // Đưa servo về vị trí ban đầu
  mg996r.write(0);
  delay(1000);
}

void loop() {

  Serial.println("Goc 0");
  mg996r.write(0);
  delay(2000);

  Serial.println("Goc 45");
  mg996r.write(45);
  delay(2000);

  Serial.println("Goc 90");
  mg996r.write(90);
  delay(2000);

  Serial.println("Goc 135");
  mg996r.write(135);
  delay(2000);

  Serial.println("Goc 180");
  mg996r.write(180);
  delay(2000);
}