#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

const uint16_t kRecvPin = 15;
const uint16_t kIrLedPin = 4;

IRrecv irrecv(kRecvPin, 1024, 50, true);
IRsend irsend(kIrLedPin);
decode_results results;

// Danh sách nút cần học (thêm/bớt tùy remote)
const char* BTN_NAMES[] = {
  "POWER", "MUTE", "INPUT", "VOL_UP", "VOL_DOWN", "CH_UP", "CH_DOWN",
  "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "HOME", "MENU",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};
const int N = sizeof(BTN_NAMES) / sizeof(BTN_NAMES[0]);
uint32_t codes[N];
bool learned[N];

// ----- Học mã -----
void learnAll() {
  Serial.println("\n=== CHE DO HOC MA ===");
  Serial.println("Bam dung nut duoc yeu cau. Go 's' + Enter de bo qua nut do.\n");
  for (int i = 0; i < N; i++) {
    Serial.printf("Bam nut [%s] ... ", BTN_NAMES[i]);
    learned[i] = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 15000) {            // cho toi da 15 giay
      if (Serial.available()) {
        char c = Serial.read();
        if (c == 's') { Serial.println("bo qua"); break; }
      }
      if (irrecv.decode(&results)) {
        if (results.decode_type == NIKAI && results.bits == 24) {
          codes[i] = (uint32_t)results.value;
          learned[i] = true;
          Serial.printf("OK 0x%06X\n", codes[i]);
          irrecv.resume();
          delay(400);                           // tranh doc lap khi giu nut
          irrecv.resume();
          break;
        }
        irrecv.resume();
      }
      delay(5);
    }
    if (!learned[i] && millis() - t0 >= 15000) Serial.println("het gio, bo qua");
  }
  Serial.println("\n=== BANG MA (copy vao code) ===");
  for (int i = 0; i < N; i++) {
    if (learned[i])
      Serial.printf("  {\"%s\", 0x%06X},\n", BTN_NAMES[i], codes[i]);
  }
  Serial.println("===============================\n");
}

// ----- Gửi mã -----
void sendByName(String name) {
  name.toUpperCase();
  for (int i = 0; i < N; i++) {
    if (learned[i] && name == BTN_NAMES[i]) {
      irsend.sendNikai(codes[i], 24, 1);
      Serial.printf("Da gui %s = 0x%06X\n", BTN_NAMES[i], codes[i]);
      return;
    }
  }
  Serial.println("Khong tim thay nut (hoac chua hoc).");
}

void setup() {
  Serial.begin(115200);
  irrecv.enableIRIn();
  irsend.begin();
  Serial.println("Go 'learn' de hoc ma, hoac go ten nut (vi du: power, vol_up) de gui.");
}

void loop() {
  static String line = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line == "learn") learnAll();
      else if (line.length()) sendByName(line);
      line = "";
    } else line += c;
  }
}