/*
 * =========================================================================================
 * DỰ ÁN: ĐIỀU KHIỂN LED RGB (CHÂN R, G, B, 5V / DƯƠNG CHUNG - COMMON ANODE) BẰNG ESP32-S3 16R8
 * HỖ TRỢ: ĐỔI MÀU, TĂNG / GIẢM ĐỘ SÁNG (BRIGHTNESS 0 - 100%), HIỆU ỨNG VÀ SERIAL MONITOR
 * =========================================================================================
 * 
 * SƠ ĐỒ ĐẤU NỐI KHUYẾN NGHỊ:
 * -----------------------------------------------------------------------------------------
 *   LED RGB Pin       Điện trở hạn dòng        Chân ESP32-S3 (N16R8)
 *   -----------       -----------------        ---------------------
 *   Chân R (Red)    ---> [Điện trở 220Ω - 330Ω] ---> GPIO 4
 *   Chân G (Green)  ---> [Điện trở 220Ω - 330Ω] ---> GPIO 5
 *   Chân B (Blue)   ---> [Điện trở 220Ω - 330Ω] ---> GPIO 6
 *   Chân 5V (Anode) -----------------------------> Chân 3V3 hoặc 5V (VIN)*
 * 
 * * LƯU Ý QUAN TRỌNG VỀ PHẦN CỨNG:
 *   1. Chân 5V của LED (Common Anode) nên cắm vào chân 3V3 của ESP32-S3 nếu cắm trực tiếp
 *      vào chân GPIO (để tránh chênh lệch điện áp 5V và logic 3.3V của ESP32 làm LED sáng mờ).
 *   2. ESP32-S3 16R8 (N16R8): Các chân GPIO 33 -> 37 được dùng cho Octal PSRAM/Flash nội,
 *      TUYỆT ĐỐI KHÔNG DÙNG các chân GPIO 33, 34, 35, 36, 37.
 * =========================================================================================
 */

#include <Arduino.h>

// =========================================================================================
// 1. CẤU HÌNH CHÂN GPIO VÀ PHẦN CỨNG
// =========================================================================================
#define PIN_RED    4   // Chân điều khiển màu Đỏ
#define PIN_GREEN  5   // Chân điều khiển màu Xanh Lá
#define PIN_BLUE   6   // Chân điều khiển màu Xanh Dương

// LED RGB có chân 5V là loại Common Anode (Dương chung).
// Nếu nối qua mạch Transistor đệm đảo mức hoặc Common Cathode, đổi thành false.
#define COMMON_ANODE true

// Tần số PWM và độ phân giải (8-bit: 0 - 255)
#define PWM_FREQ       5000   // 5 kHz
#define PWM_RESOLUTION 8      // 8-bit (0 -> 255)

// =========================================================================================
// 2. BIẾN TRẠNG THÁI TOÀN CỤC
// =========================================================================================
// Màu gốc (0 - 255 cho mỗi kênh)
uint8_t baseR = 255;
uint8_t baseG = 255;
uint8_t baseB = 255;

// Độ sáng tổng thể (Master Brightness: 0% -> 100%)
uint8_t currentBrightness = 100;

// Chế độ hiện tại: "manual", "rainbow", "breathe", "demo"
String currentMode = "manual";

// =========================================================================================
// 3. CÁC HÀM XUẤT PWM VÀ ĐIỀU KHIỂN ĐỘ SÁNG / MÀU SẮC
// =========================================================================================

/**
 * Tính toán duty cycle dựa trên màu cơ sở, độ sáng hiện tại và chuẩn Anode/Cathode
 */
void applyColor() {
  // Tính độ sáng thực tế: Giá trị gốc * (Độ sáng % / 100)
  uint16_t scaledR = ((uint16_t)baseR * currentBrightness) / 100;
  uint16_t scaledG = ((uint16_t)baseG * currentBrightness) / 100;
  uint16_t scaledB = ((uint16_t)baseB * currentBrightness) / 100;

  uint8_t dutyR = (uint8_t)scaledR;
  uint8_t dutyG = (uint8_t)scaledG;
  uint8_t dutyB = (uint8_t)scaledB;

  // Với Common Anode (chân chung nối 5V/3.3V):
  // Mức LOW (duty 0) = Sáng nhất, Mức HIGH (duty 255) = Tắt
  if (COMMON_ANODE) {
    dutyR = 255 - dutyR;
    dutyG = 255 - dutyG;
    dutyB = 255 - dutyB;
  }

  analogWrite(PIN_RED, dutyR);
  analogWrite(PIN_GREEN, dutyG);
  analogWrite(PIN_BLUE, dutyB);
}

/**
 * Đặt màu RGB (0 - 255 cho mỗi kênh)
 */
void setColor(uint8_t red, uint8_t green, uint8_t blue) {
  baseR = red;
  baseG = green;
  baseB = blue;
  applyColor();
}

/**
 * Đặt độ sáng trực tiếp (0% -> 100%)
 */
void setBrightness(int percent) {
  currentBrightness = (uint8_t)constrain(percent, 0, 100);
  applyColor();
  Serial.printf("[LED] >> Do sang hien tai: %d%%\n", currentBrightness);
}

/**
 * Tăng độ sáng (mặc định tăng 10%)
 */
void increaseBrightness(int step = 10) {
  int newBri = (int)currentBrightness + step;
  if (newBri > 100) newBri = 100;

  // Nếu đèn đang tắt hoàn toàn cả 3 màu (0,0,0), khi tăng độ sáng sẽ chuyển sang màu Trắng
  if (baseR == 0 && baseG == 0 && baseB == 0) {
    baseR = 255;
    baseG = 255;
    baseB = 255;
  }

  setBrightness(newBri);
}

/**
 * Giảm độ sáng (mặc định giảm 10%)
 */
void decreaseBrightness(int step = 10) {
  int newBri = (int)currentBrightness - step;
  if (newBri < 0) newBri = 0;
  setBrightness(newBri);
}

/**
 * Tắt toàn bộ đèn LED
 */
void turnOff() {
  setColor(0, 0, 0);
  currentMode = "manual";
  Serial.println("[LED] Da tat den.");
}

/**
 * Đặt màu từ mã HEX (VD: "#FF5733" hoặc "00FF00")
 */
void setHexColor(String hexCode) {
  if (hexCode.startsWith("#")) {
    hexCode.remove(0, 1);
  }
  if (hexCode.length() != 6) {
    Serial.println("[ERROR] Ma mau HEX khong hop le! Vui long nhap dang #RRGGBB (VD: #FF00FF)");
    return;
  }

  long number = strtol(hexCode.c_str(), NULL, 16);
  uint8_t r = (number >> 16) & 0xFF;
  uint8_t g = (number >> 8) & 0xFF;
  uint8_t b = number & 0xFF;

  currentMode = "manual";
  setColor(r, g, b);
  Serial.printf("[LED] Mau HEX #%s -> R:%d, G:%d, B:%d (Do sang: %d%%)\n", 
                hexCode.c_str(), r, g, b, currentBrightness);
}

// =========================================================================================
// 4. CÁC HIỆU ỨNG ÁNH SÁNG
// =========================================================================================

/**
 * Chuyển đổi bánh xe màu Wheel (0 - 255) sang RGB
 */
void wheelColor(uint8_t wheelPos, uint8_t &r, uint8_t &g, uint8_t &b) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) {
    r = 255 - wheelPos * 3;
    g = 0;
    b = wheelPos * 3;
  } else if (wheelPos < 170) {
    wheelPos -= 85;
    r = 0;
    g = wheelPos * 3;
    b = 255 - wheelPos * 3;
  } else {
    wheelPos -= 170;
    r = wheelPos * 3;
    g = 255 - wheelPos * 3;
    b = 0;
  }
}

/**
 * Hiệu ứng chuyển màu cầu vồng (tự động theo độ sáng hiện tại)
 */
void runRainbowStep() {
  static uint8_t pos = 0;
  uint8_t r, g, b;
  wheelColor(pos++, r, g, b);
  setColor(r, g, b);
  delay(20);
}

/**
 * Hiệu ứng nhịp thở (Breathing effect, đỉnh thở tương ứng với currentBrightness)
 */
void runBreathingStep() {
  static int breathFactor = 0; // 0 -> 100
  static int breathDir = 2;
  
  breathFactor += breathDir;
  if (breathFactor <= 0 || breathFactor >= 100) {
    breathDir = -breathDir;
  }
  breathFactor = constrain(breathFactor, 0, 100);

  // Lấy màu hiện tại (nếu đang là 0,0,0 thì dùng màu Cyan)
  uint8_t effR = (baseR == 0 && baseG == 0 && baseB == 0) ? 0   : baseR;
  uint8_t effG = (baseR == 0 && baseG == 0 && baseB == 0) ? 255 : baseG;
  uint8_t effB = (baseR == 0 && baseG == 0 && baseB == 0) ? 255 : baseB;

  // Tính độ sáng kết hợp: (Master Brightness * Breath Factor) / 100
  uint16_t scale = ((uint16_t)currentBrightness * breathFactor) / 100;
  uint8_t dutyR = ((uint16_t)effR * scale) / 100;
  uint8_t dutyG = ((uint16_t)effG * scale) / 100;
  uint8_t dutyB = ((uint16_t)effB * scale) / 100;

  if (COMMON_ANODE) {
    dutyR = 255 - dutyR;
    dutyG = 255 - dutyG;
    dutyB = 255 - dutyB;
  }

  analogWrite(PIN_RED, dutyR);
  analogWrite(PIN_GREEN, dutyG);
  analogWrite(PIN_BLUE, dutyB);

  delay(15);
}

/**
 * Chạy demo các màu cơ bản
 */
void runDemoStep() {
  struct ColorItem {
    const char* name;
    uint8_t r, g, b;
  };

  const ColorItem colors[] = {
    {"DO (Red)",         255, 0,   0},
    {"XANH LA (Green)",  0,   255, 0},
    {"XANH DUONG (Blue)",0,   0,   255},
    {"VANG (Yellow)",    255, 255, 0},
    {"TIM (Purple)",     180, 0,   255},
    {"XANH NHO (Cyan)",  0,   255, 255},
    {"TRANG (White)",    255, 255, 255}
  };
  const size_t numColors = sizeof(colors) / sizeof(colors[0]);

  static size_t idx = 0;
  static unsigned long lastChange = 0;

  if (millis() - lastChange > 1200) {
    lastChange = millis();
    Serial.printf("[DEMO] Dang bat mau: %s (Do sang: %d%%)\n", colors[idx].name, currentBrightness);
    setColor(colors[idx].r, colors[idx].g, colors[idx].b);
    idx = (idx + 1) % numColors;
  }
}

// =========================================================================================
// 5. XỬ LÝ LỆNH TỪ SERIAL MONITOR
// =========================================================================================
void printHelp() {
  Serial.println("\n----------------- DANH SACH LENH DIEU KHIEN -----------------");
  Serial.println("  1. Mau co ban  : red, green, blue, yellow, purple, cyan, white, off");
  Serial.println("  2. Do sang     : '+' (tang 10%), '-' (giam 10%)");
  Serial.println("                 : 'bri 50', 'b 80' hoac '50%' (chinh 0 - 100%)");
  Serial.println("  3. Ma mau HEX  : #FF5733 hoac FF0000");
  Serial.println("  4. Mau RGB     : rgb(255,100,50) hoac 255,100,50");
  Serial.println("  5. Hieu ung    : rainbow (cau vong), breathe (nhip tho), demo (chay thu)");
  Serial.println("-------------------------------------------------------------\n");
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) return;

  // Lệnh trợ giúp
  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  }

  // --- TĂNG / GIẢM ĐỘ SÁNG ---
  if (cmd == "+" || cmd == "up" || cmd == "tang") {
    increaseBrightness(10);
    return;
  }
  if (cmd == "-" || cmd == "down" || cmd == "giam") {
    decreaseBrightness(10);
    return;
  }
  if (cmd.startsWith("bri ") || cmd.startsWith("brightness ") || cmd.startsWith("b ")) {
    int spaceIdx = cmd.indexOf(' ');
    int val = cmd.substring(spaceIdx + 1).toInt();
    setBrightness(val);
    return;
  }
  if (cmd.endsWith("%")) {
    int val = cmd.substring(0, cmd.length() - 1).toInt();
    setBrightness(val);
    return;
  }

  // --- BẬT / TẮT & ĐỔI MÀU ---
  if (cmd == "off") {
    turnOff();
  } else if (cmd == "red") {
    currentMode = "manual";
    setColor(255, 0, 0);
    Serial.printf("[LED] Mau Do (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "green") {
    currentMode = "manual";
    setColor(0, 255, 0);
    Serial.printf("[LED] Mau Xanh La (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "blue") {
    currentMode = "manual";
    setColor(0, 0, 255);
    Serial.printf("[LED] Mau Xanh Duong (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "yellow") {
    currentMode = "manual";
    setColor(255, 255, 0);
    Serial.printf("[LED] Mau Vang (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "purple") {
    currentMode = "manual";
    setColor(180, 0, 255);
    Serial.printf("[LED] Mau Tim (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "cyan") {
    currentMode = "manual";
    setColor(0, 255, 255);
    Serial.printf("[LED] Mau Xanh Lo (Cyan) (Do sang: %d%%)\n", currentBrightness);
  } else if (cmd == "white") {
    currentMode = "manual";
    setColor(255, 255, 255);
    Serial.printf("[LED] Mau Trang (Do sang: %d%%)\n", currentBrightness);
  } 
  // --- HIỆU ỨNG ---
  else if (cmd == "rainbow") {
    currentMode = "rainbow";
    Serial.println("[LED] Bat hieu ung Cau Vong (Rainbow)");
  } else if (cmd == "breathe") {
    currentMode = "breathe";
    Serial.println("[LED] Bat hieu ung Nhip Tho (Breathing)");
  } else if (cmd == "demo") {
    currentMode = "demo";
    Serial.println("[LED] Bat che do tu dong chuyen mau Demo");
  } 
  // --- MÃ HEX HOẶC RGB ---
  else if (cmd.startsWith("#") || (cmd.length() == 6 && isHexadecimalDigit(cmd[0]))) {
    setHexColor(cmd);
  } else if (cmd.indexOf(',') != -1) {
    if (cmd.startsWith("rgb(") && cmd.endsWith(")")) {
      cmd = cmd.substring(4, cmd.length() - 1);
    }
    int firstComma = cmd.indexOf(',');
    int secondComma = cmd.indexOf(',', firstComma + 1);
    if (firstComma != -1 && secondComma != -1) {
      int r = cmd.substring(0, firstComma).toInt();
      int g = cmd.substring(firstComma + 1, secondComma).toInt();
      int b = cmd.substring(secondComma + 1).toInt();
      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);
      currentMode = "manual";
      setColor(r, g, b);
      Serial.printf("[LED] Mau tuy chinh -> R:%d, G:%d, B:%d (Do sang: %d%%)\n", 
                    r, g, b, currentBrightness);
    } else {
      Serial.println("[ERROR] Sai dinh dang RGB! Vui long nhap dang: 255,100,50");
    }
  } else {
    Serial.println("[ERROR] Lenh khong hop le! Go 'help' de xem danh sach lenh.");
  }
}

// =========================================================================================
// 6. ARDUINO SETUP & LOOP
// =========================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================================");
  Serial.println("  ESP32-S3 (16R8) - RGB LED CONTROLLER (5V/ANODE)");
  Serial.println("  TICH HOP DIEU KHIEN DO SANG & HIEN THI SERIAL");
  Serial.println("==============================================");

  // Cau hinh chan xuat PWM
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);

  // Dat tan so va do phan giai PWM (ESP32 Core 3.x ho tro theo tung pin)
  analogWriteFrequency(PIN_RED, PWM_FREQ);
  analogWriteFrequency(PIN_GREEN, PWM_FREQ);
  analogWriteFrequency(PIN_BLUE, PWM_FREQ);
  analogWriteResolution(PIN_RED, PWM_RESOLUTION);
  analogWriteResolution(PIN_GREEN, PWM_RESOLUTION);
  analogWriteResolution(PIN_BLUE, PWM_RESOLUTION);

  // Ban dau tat den
  turnOff();

  printHelp();
  Serial.println("-> San sang nhan lenh qua Serial Monitor...");
}

void loop() {
  // 1. Doc lenh tu Serial Monitor
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    handleSerialCommand(input);
  }

  // 2. Chay hieu ung tuong ung (neu dang o mode hieu ung)
  if (currentMode == "rainbow") {
    runRainbowStep();
  } else if (currentMode == "breathe") {
    runBreathingStep();
  } else if (currentMode == "demo") {
    runDemoStep();
  }
}
