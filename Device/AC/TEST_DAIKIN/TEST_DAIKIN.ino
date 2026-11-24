/*
 * BƯỚC 2: Điều khiển điều hòa Daikin bằng ESP32-S3 + IR LED 940nm + 2N2222.
 *
 * Mạch phát:
 *   GPIO 4 --[1kΩ]--> Base 2N2222
 *   Emitter 2N2222 --> GND
 *   5V --[47..100Ω]--> Anode IR LED, Cathode --> Collector 2N2222
 *   (nên có tụ 100µF gần LED vì xung dòng lớn)
 *
 * Lưu ý ESP32-S3 N16R8: KHÔNG dùng GPIO 26-37 (flash/PSRAM octal),
 * tránh GPIO 0, 3, 45, 46 (strapping). GPIO 4 là an toàn.
 *
 * Arduino IDE: Board "ESP32S3 Dev Module", Flash Size 16MB,
 * PSRAM "OPI PSRAM", USB CDC On Boot "Enabled".
 *
 * Mặc định dùng giao thức Daikin chuẩn (ARC433xx, 280 bit) - phổ biến nhất.
 * Nếu bước 1 báo giao thức khác, đổi lớp IRDaikinESP bên dưới (xem README/ghi chú).
 *
 * Lệnh gõ trong Serial Monitor (115200, kết thúc bằng Enter):
 *   on | off
 *   t24            -> đặt 24°C (18..30)
 *   cool | heat | dry | fan | auto
 *   f1..f5 | fauto | fquiet     -> tốc độ quạt
 *   swing1 | swing0             -> đảo gió dọc bật/tắt
 *   powerful | econo            -> chế độ mạnh / tiết kiệm (bật)
 */
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>

const uint16_t kIrLedPin = 4;
IRDaikinESP ac(kIrLedPin);

void sendState() {
  ac.send();
  Serial.println("Da gui: " + ac.toString());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  ac.begin();

  // Trạng thái mặc định
  ac.off();
  ac.setFan(kDaikinFanAuto);
  ac.setMode(kDaikinCool);
  ac.setTemp(25);
  ac.setSwingVertical(false);
  ac.setSwingHorizontal(false);
  ac.setQuiet(false);
  ac.setPowerful(false);
  ac.setEcono(false);

  Serial.println("San sang. Go lenh: on, off, t24, cool, heat, f3...");
}

void loop() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "on") ac.on();
  else if (cmd == "off") ac.off();
  else if (cmd == "cool") ac.setMode(kDaikinCool);
  else if (cmd == "heat") ac.setMode(kDaikinHeat);
  else if (cmd == "dry")  ac.setMode(kDaikinDry);
  else if (cmd == "fan")  ac.setMode(kDaikinFan);
  else if (cmd == "auto") ac.setMode(kDaikinAuto);
  else if (cmd == "fauto")  ac.setFan(kDaikinFanAuto);
  else if (cmd == "fquiet") ac.setFan(kDaikinFanQuiet);
  else if (cmd.startsWith("f") && cmd.length() == 2) {
    int f = cmd.substring(1).toInt();           // 1..5
    if (f >= 1 && f <= 5) ac.setFan(f);
  }
  else if (cmd.startsWith("t")) {
    int t = cmd.substring(1).toInt();
    if (t >= 18 && t <= 30) ac.setTemp(t);
    else { Serial.println("Nhiet do hop le: 18..30"); return; }
  }
  else if (cmd == "swing1") ac.setSwingVertical(true);
  else if (cmd == "swing0") ac.setSwingVertical(false);
  else if (cmd == "powerful") ac.setPowerful(true);
  else if (cmd == "econo")    ac.setEcono(true);
  else { Serial.println("Lenh khong hop le"); return; }

  sendState();
}
