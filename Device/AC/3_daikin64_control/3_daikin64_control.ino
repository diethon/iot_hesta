/*
 * Điều khiển điều hòa Daikin dùng giao thức DAIKIN64 (64 bit) bằng ESP32-S3.
 *
 * Mạch phát: GPIO 4 --[1kΩ]--> Base 2N2222, Emitter -> GND,
 *            5V --[220Ω]--> Anode IR LED, Cathode -> Collector.
 *
 * QUAN TRỌNG: DAIKIN64 dùng bit "Power Toggle" (đảo trạng thái bật/tắt),
 * KHÔNG phải lệnh on/off riêng. Vì vậy:
 *   - Chỉ lệnh "power" mới gửi bit toggle = 1 (bật nếu đang tắt, tắt nếu đang bật).
 *   - Các lệnh khác (nhiệt độ, chế độ, quạt...) luôn gửi toggle = 0
 *     để không vô tình bật/tắt máy.
 *
 * Lệnh Serial (115200, Enter):
 *   power                  -> bật/tắt máy (toggle)
 *   t16..t30               -> nhiệt độ
 *   cool | heat | dry | fan
 *   fauto | f1 | f2 | f3 | fturbo | fquiet   (f1=thấp, f2=vừa, f3=cao)
 *   swing1 | swing0        -> đảo gió dọc
 *   raw                    -> phát lại đúng gói bạn đã bắt được (test phần cứng,
 *                             Cool 26°C, quạt thấp, kèm Power Toggle)
 */
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>

const uint16_t kIrLedPin = 4;
IRDaikin64 ac(kIrLedPin);

// Gói raw đã bắt được từ remote (Cool, 26C, Fan Low, Power Toggle)
uint16_t rawData[137] = {9774, 9814,  9796, 9832,  4568, 2526,  336, 374,  356, 952,  356, 952,  356, 374,  356, 952,  356, 374,  356, 374,  356, 374,  356, 374,  356, 952,  356, 374,  356, 374,  356, 374,  356, 374,  356, 372,  356, 952,  356, 374,  356, 952,  356, 374,  356, 374,  354, 374,  356, 374,  356, 372,  356, 374,  356, 374,  356, 952,  354, 954,  354, 374,  356, 374,  356, 374,  356, 372,  356, 374,  358, 374,  356, 374,  356, 374,  356, 374,  356, 952,  356, 374,  356, 374,  356, 372,  358, 374,  356, 374,  358, 372,  356, 374,  356, 954,  354, 374,  356, 372,  356, 374,  356, 374,  356, 952,  356, 954,  356, 374,  356, 374,  356, 952,  356, 374,  356, 374,  356, 952,  356, 374,  356, 952,  356, 952,  356, 372,  356, 374,  356, 374,  356, 374,  356, 20324,  4568};

IRsend irsend(kIrLedPin);  // dùng chung chân cho lệnh raw

void sendState(bool toggle) {
  ac.setPowerToggle(toggle);
  ac.send();
  ac.setPowerToggle(false);  // các lệnh sau không được làm đảo nguồn
  Serial.println("Da gui: " + ac.toString());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  ac.begin();
  irsend.begin();

  ac.setPowerToggle(false);
  ac.setMode(kDaikin64Cool);
  ac.setTemp(26);
  ac.setFan(kDaikin64FanLow);
  ac.setSwingVertical(false);

  Serial.println("San sang. Go lenh: power, t26, cool, f1, swing1, raw ...");
}

void loop() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "power") { sendState(true); return; }

  if (cmd == "raw") {
    irsend.sendRaw(rawData, 137, 38);  // 38 kHz
    Serial.println("Da phat lai goi raw (se dao nguon bat/tat).");
    return;
  }

  if (cmd == "cool") ac.setMode(kDaikin64Cool);
  else if (cmd == "heat") ac.setMode(kDaikin64Heat);
  else if (cmd == "dry")  ac.setMode(kDaikin64Dry);
  else if (cmd == "fan")  ac.setMode(kDaikin64Fan);
  else if (cmd == "fauto")  ac.setFan(kDaikin64FanAuto);
  else if (cmd == "f1")     ac.setFan(kDaikin64FanLow);
  else if (cmd == "f2")     ac.setFan(kDaikin64FanMed);
  else if (cmd == "f3")     ac.setFan(kDaikin64FanHigh);
  else if (cmd == "fturbo") ac.setFan(kDaikin64FanTurbo);
  else if (cmd == "fquiet") ac.setFan(kDaikin64FanQuiet);
  else if (cmd == "swing1") ac.setSwingVertical(true);
  else if (cmd == "swing0") ac.setSwingVertical(false);
  else if (cmd.startsWith("t")) {
    int t = cmd.substring(1).toInt();
    if (t < 16 || t > 30) { Serial.println("Nhiet do hop le: 16..30"); return; }
    ac.setTemp(t);
  }
  else { Serial.println("Lenh khong hop le"); return; }

  sendState(false);
}
