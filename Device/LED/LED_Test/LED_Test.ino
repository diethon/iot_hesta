/*
 * ====================================================================================================
 * DỰ ÁN: HESTA / SYNA SMART HOME - TEST PHẦN CỨNG 5 KÊNH ĐÈN QUA MOSFET (BẬT TẮT & DIMMING ĐỘ SÁNG)
 * FILE : LED_Test.ino
 * ====================================================================================================
 * 
 * ĐẶC ĐIỂM NỔI BẬT:
 * 1. CẮM ĐIỆN LÀ SÁNG NGAY: Toàn bộ 5 đèn tự động BẬT SÁNG 100% ngay khi cắm nguồn (không cần gõ lệnh).
 * 2. STANDALONE: Hoàn toàn không cần kết nối Wi-Fi, BLE hay MQTT. Nạp là chạy ngay lập tức.
 * 3. HỖ TRỢ ĐẦY ĐỦ: Bật / Tắt và TĂNG / GIẢM ĐỘ SÁNG (0% -> 100%) qua xung PWM 5000Hz mịn màng.
 * 4. TƯƠNG THÍCH HOÀN HẢO: ESP32 và ESP32-S3 N16R8 (cả Arduino Core 2.x và Core 3.x).
 * 
 * 1. DANH SÁCH CHÂN GPIO & ĐỊNH DANH THIẾT BỊ:
 * ----------------------------------------------------------------------------------------------------
 * - NODE ID  : 32442d7f-dfcf-4b2f-a380-adba32876cec
 * - DEVICE 1 : 19f79c42-afc1-4796-a48c-8d076ea45b65  ---> Chân GPIO 4 (Kênh PWM 0)
 * - DEVICE 2 : 98ae5452-250c-44c6-8b1a-fac08b2fc716  ---> Chân GPIO 5 (Kênh PWM 1)
 * - DEVICE 3 : c8186ca5-ff09-486c-bc56-ac75ef85e6d9  ---> Chân GPIO 7 (Kênh PWM 2)
 * - DEVICE 4 : c968b5c4-80d1-4aec-abef-cc52876a6da5  ---> Chân GPIO 8 (Kênh PWM 3)
 * (Lưu ý: Đèn b016fe3d-36fc-4d3e-a8bd-e287d7138395 đã được chuyển sang Node Sensor_Motor_Gate_RollingDoor)
 * 
 * 2. SƠ ĐỒ ĐẤU NỐI VỚI MODULE MOSFET:
 * ----------------------------------------------------------------------------------------------------
 *       ESP32-S3 (3.3V)                             Module MOSFET Kênh N
 *   +------------------+                        +---------------------------+
 *   |                  |                        |                           |
 *   |  GPIO 4/5/6/7/8  |---> [Điện trở 100Ω]--->| GATE (G)                  |
 *   |                  |           |            |                           |
 *   |                  |   [Trở xả 10kΩ xuống GND]                          |
 *   |                  |           |            |                           |
 *   |  GND             |----------------------->| SOURCE (S) / GND Module   |
 *   +------------------+           |            |                           |
 *                                  |            | DRAIN (D) ----------------+---> Cực ÂM (-) của Đèn LED
 *   Nguồn ngoài LED                |            +---------------------------+
 *   +------------------+           |
 *   |  GND (-)         |-----------+ (BẮT BUỘC NỐI CHUNG GND GIỮA ESP32 VÀ NGUỒN LED)
 *   |  VCC (+12V/+24V) |---------------------------------------------------------> Cực DƯƠNG (+) của Đèn LED
 *   +------------------+
 * ====================================================================================================
 */

#include <Arduino.h>

// ====================================================================================================
// 1. CẤU HÌNH PHẦN CỨNG & THIẾT BỊ
// ====================================================================================================
#define NUM_LEDS 4

const uint8_t LED_PINS[NUM_LEDS]     = {4, 5, 7, 8};
const uint8_t PWM_CHANNELS[NUM_LEDS] = {0, 1, 2, 3};

const char* DEVICE_IDS[NUM_LEDS] = {
    "19f79c42-afc1-4796-a48c-8d076ea45b65",
    "98ae5452-250c-44c6-8b1a-fac08b2fc716",
    "c8186ca5-ff09-486c-bc56-ac75ef85e6d9",
    "c968b5c4-80d1-4aec-abef-cc52876a6da5"
};

// Cực tính MOSFET:
// true : Mức HIGH (3.3V) -> MOSFET dẫn -> Đèn BẬT (mặc định module N-MOSFET)
// false: Mức LOW (0V) -> Đèn BẬT (nếu mạch có optocoupler đảo mức hoặc P-MOSFET)
#define MOSFET_ACTIVE_HIGH true

// Tần số PWM: 5000Hz (5kHz) và độ phân giải 8-bit (0 - 255)
#define PWM_FREQ       500
#define PWM_RESOLUTION 8
#define PWM_MAX_DUTY   255

// Trạng thái của từng đèn
bool    ledPower[NUM_LEDS]          = {true, true, true, true};  // Mặc định BẬT khi cắm nguồn
uint8_t ledBrightness[NUM_LEDS]     = {100, 100, 100, 100};       // Mặc định sáng 100%
uint8_t ledLastBrightness[NUM_LEDS] = {100, 100, 100, 100};       // Nhớ mức sáng trước khi tắt

// Biến cho chế độ tự động thở (Fade/Breathing Demo)
bool autoFadeDemo = false;
int  fadeDirection = 1;
int  fadeValue = 100;
unsigned long lastFadeTime = 0;

// ====================================================================================================
// 2. KHỞI TẠO VÀ XUẤT PWM (TƯƠNG THÍCH CẢ ARDUINO CORE 2.x VÀ 3.x)
// ====================================================================================================
void initPWM() {
    for (int i = 0; i < NUM_LEDS; i++) {
        pinMode(LED_PINS[i], OUTPUT);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        // Core 3.x: Gắn PWM trực tiếp vào chân GPIO
        ledcAttach(LED_PINS[i], PWM_FREQ, PWM_RESOLUTION);
#else
        // Core 2.x: Cấu hình qua kênh PWM
        ledcSetup(PWM_CHANNELS[i], PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(LED_PINS[i], PWM_CHANNELS[i]);
#endif
    }
}

void writeHardwareDuty(uint8_t index, uint8_t duty) {
    if (index >= NUM_LEDS) return;

    uint8_t physicalDuty = duty;
    if (!MOSFET_ACTIVE_HIGH) {
        physicalDuty = PWM_MAX_DUTY - duty;
    }

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(LED_PINS[index], physicalDuty);
#else
    ledcWrite(PWM_CHANNELS[index], physicalDuty);
#endif
}

/**
 * Cập nhật giá trị PWM thực tế ra chân MOSFET dựa vào Power và Brightness
 */
void applyLedOutput(uint8_t index) {
    if (index >= NUM_LEDS) return;

    uint8_t duty = 0;
    if (ledPower[index] && ledBrightness[index] > 0) {
        duty = (uint8_t)(((uint16_t)ledBrightness[index] * PWM_MAX_DUTY) / 100);
    }
    writeHardwareDuty(index, duty);
}

// ====================================================================================================
// 3. CÁC HÀM ĐIỀU KHIỂN BẬT / TẮT & TĂNG / GIẢM ĐỘ SÁNG
// ====================================================================================================

/**
 * Bật hoặc Tắt đèn tại chỉ mục index (0..4)
 */
void setPower(uint8_t index, bool on) {
    if (index >= NUM_LEDS) return;
    ledPower[index] = on;

    if (on) {
        // Nếu trước đó độ sáng = 0 thì khôi phục độ sáng đã nhớ hoặc 100%
        if (ledBrightness[index] == 0) {
            ledBrightness[index] = (ledLastBrightness[index] > 0) ? ledLastBrightness[index] : 100;
        }
    } else {
        // Lưu lại độ sáng trước khi tắt
        if (ledBrightness[index] > 0) {
            ledLastBrightness[index] = ledBrightness[index];
        }
    }

    applyLedOutput(index);
    Serial.printf("[ĐÈN %d - GPIO %d] -> %s | Độ sáng: %d%%\n", 
                  index + 1, LED_PINS[index], 
                  ledPower[index] ? "BẬT (ON)" : "TẮT (OFF)", 
                  ledBrightness[index]);
}

/**
 * Đặt độ sáng trực tiếp (0 - 100%)
 */
void setBrightness(uint8_t index, int percent) {
    if (index >= NUM_LEDS) return;

    percent = constrain(percent, 0, 100);
    ledBrightness[index] = (uint8_t)percent;

    if (percent > 0) {
        ledPower[index] = true;
        ledLastBrightness[index] = (uint8_t)percent;
    } else {
        ledPower[index] = false;
    }

    applyLedOutput(index);
    Serial.printf("[ĐÈN %d - GPIO %d] -> Độ sáng: %d%% (%s)\n", 
                  index + 1, LED_PINS[index], 
                  ledBrightness[index], 
                  ledPower[index] ? "ON" : "OFF");
}

/**
 * Tăng / Giảm độ sáng theo bước (VD: +10% hoặc -10%)
 */
void stepBrightness(uint8_t index, int delta) {
    if (index >= NUM_LEDS) return;

    int current = ledPower[index] ? ledBrightness[index] : 0;
    int newBri = current + delta;
    newBri = constrain(newBri, 0, 100);

    setBrightness(index, newBri);
}

/**
 * Đảo trạng thái Bật <-> Tắt (Toggle)
 */
void toggleLed(uint8_t index) {
    if (index >= NUM_LEDS) return;
    setPower(index, !ledPower[index]);
}

/**
 * Bật toàn bộ 5 đèn
 */
void turnOnAll() {
    Serial.println("\n[TẤT CẢ] >>> BẬT TOÀN BỘ 5 ĐÈN <<<");
    for (int i = 0; i < NUM_LEDS; i++) {
        setPower(i, true);
    }
}

/**
 * Tắt toàn bộ 5 đèn
 */
void turnOffAll() {
    Serial.println("\n[TẤT CẢ] >>> TẮT TOÀN BỘ 5 ĐÈN <<<");
    for (int i = 0; i < NUM_LEDS; i++) {
        setPower(i, false);
    }
}

/**
 * Đặt độ sáng cho toàn bộ 5 đèn
 */
void setAllBrightness(int percent) {
    percent = constrain(percent, 0, 100);
    Serial.printf("\n[TẤT CẢ] >>> ĐẶT ĐỘ SÁNG TOÀN BỘ 5 ĐÈN: %d%% <<<\n", percent);
    for (int i = 0; i < NUM_LEDS; i++) {
        setBrightness(i, percent);
    }
}

/**
 * Tăng / Giảm độ sáng cho toàn bộ 5 đèn
 */
void stepAllBrightness(int delta) {
    Serial.printf("\n[TẤT CẢ] >>> %s ĐỘ SÁNG CẢ 5 ĐÈN (%+d%%) <<<\n", 
                  (delta >= 0) ? "TĂNG" : "GIẢM", delta);
    for (int i = 0; i < NUM_LEDS; i++) {
        stepBrightness(i, delta);
    }
}

// ====================================================================================================
// 4. CHẾ ĐỘ AUTO-FADE (DEMO TĂNG GIẢM ĐỘ SÁNG TỰ ĐỘNG)
// ====================================================================================================
void runAutoFadeStep() {
    if (!autoFadeDemo) return;

    unsigned long now = millis();
    if (now - lastFadeTime < 25) return; // Mỗi 25ms tăng/giảm 1 bước
    lastFadeTime = now;

    fadeValue += fadeDirection * 2;
    if (fadeValue >= 100) {
        fadeValue = 100;
        fadeDirection = -1;
    } else if (fadeValue <= 0) {
        fadeValue = 0;
        fadeDirection = 1;
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        ledBrightness[i] = (uint8_t)fadeValue;
        ledPower[i] = (fadeValue > 0);
        applyLedOutput(i);
    }
}

// ====================================================================================================
// 5. GIAO DIỆN LỆNH QUA SERIAL MONITOR (115200 BAUD)
// ====================================================================================================
void printHelp() {
    Serial.println("\n======================= HƯỚNG DẪN LỆNH TEST =======================");
    Serial.println("  1. Điều khiển từng đèn riêng lẻ (1 -> 4):");
    Serial.println("     - Bật / Tắt     : '1 on', '1 off', '2 on', '2 off' ...");
    Serial.println("     - Đảo trạng thái: '1' (hoặc '2', '3', '4')");
    Serial.println("     - Chỉnh độ sáng : '1 bri 50', '2 b 80', '3 40%' (0% đến 100%)");
    Serial.println("     - Tăng / Giảm   : '1 +', '1 -' (tăng/giảm 10%)");
    Serial.println("  2. Điều khiển đồng loạt cả 4 đèn:");
    Serial.println("     - 'all on' / 'on'     : Bật cả 4 đèn");
    Serial.println("     - 'all off' / 'off'   : Tắt cả 4 đèn");
    Serial.println("     - 'all bri 60'        : Đặt độ sáng cả 4 đèn thành 60%");
    Serial.println("     - 'all +' / 'all -'   : Tăng / Giảm độ sáng cả 4 đèn thêm 10%");
    Serial.println("  3. Chế độ biểu diễn tự động:");
    Serial.println("     - 'fade'              : Bật / Tắt hiệu ứng tự động sáng tối dần cả 4 đèn");
    Serial.println("  4. Tiện ích:");
    Serial.println("     - 'status'            : Xem bảng độ sáng và trạng thái hiện tại");
    Serial.println("     - 'help' hoặc '?'     : Xem lại hướng dẫn này");
    Serial.println("===================================================================\n");
}

void printStatus() {
    Serial.println("\n----------------- BẢNG TRẠNG THÁI HIỆN TẠI -----------------");
    Serial.printf("Node ID      : 32442d7f-dfcf-4b2f-a380-adba32876cec\n");
    Serial.printf("Chế độ Fade  : %s\n", autoFadeDemo ? "ĐANG CHẠY" : "DỪNG");
    Serial.println("------------------------------------------------------------");
    Serial.println("KÊNH | CHÂN GPIO | TRẠNG THÁI | ĐỘ SÁNG | DUTY PWM | DEVICE ID");
    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t duty = (ledPower[i] && ledBrightness[i] > 0) ? (uint8_t)(((uint16_t)ledBrightness[i] * PWM_MAX_DUTY) / 100) : 0;
        Serial.printf(" [%d] | GPIO %-3d  | %-10s | %3d%%    | %3d/255  | %s\n",
                      i + 1, LED_PINS[i],
                      ledPower[i] ? "BẬT (ON)" : "TẮT (OFF)",
                      ledBrightness[i],
                      duty,
                      DEVICE_IDS[i]);
    }
    Serial.println("------------------------------------------------------------\n");
}

void handleSerialCommands() {
    if (Serial.available() <= 0) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    String lowerCmd = cmd;
    lowerCmd.toLowerCase();

    // Dừng chế độ Fade tự động nếu người dùng nhập lệnh điều khiển tay
    if (lowerCmd != "fade" && lowerCmd != "help" && lowerCmd != "?" && lowerCmd != "status") {
        if (autoFadeDemo) {
            autoFadeDemo = false;
            Serial.println("[FADE] Đã dừng chế độ tự động Fade.");
        }
    }

    if (lowerCmd == "help" || lowerCmd == "?") {
        printHelp();
        return;
    }

    if (lowerCmd == "status" || lowerCmd == "info") {
        printStatus();
        return;
    }

    if (lowerCmd == "fade" || lowerCmd == "breathe") {
        autoFadeDemo = !autoFadeDemo;
        Serial.printf("[FADE] Chế độ tự động tăng giảm độ sáng: %s\n", autoFadeDemo ? "BẬT" : "TẮT");
        return;
    }

    // Lệnh cho tất cả đèn: "all on", "all off", "all bri 50", "all +", "all -"
    if (lowerCmd == "all on" || lowerCmd == "on" || lowerCmd == "bat") {
        turnOnAll();
        return;
    }
    if (lowerCmd == "all off" || lowerCmd == "off" || lowerCmd == "tat") {
        turnOffAll();
        return;
    }
    if (lowerCmd == "all +" || lowerCmd == "all up") {
        stepAllBrightness(10);
        return;
    }
    if (lowerCmd == "all -" || lowerCmd == "all down") {
        stepAllBrightness(-10);
        return;
    }
    if (lowerCmd.startsWith("all bri ") || lowerCmd.startsWith("all b ")) {
        int spaceIdx = lowerCmd.lastIndexOf(' ');
        int val = lowerCmd.substring(spaceIdx + 1).toInt();
        setAllBrightness(val);
        return;
    }
    if (lowerCmd.startsWith("all ") && lowerCmd.endsWith("%")) {
        int val = lowerCmd.substring(4, lowerCmd.length() - 1).toInt();
        setAllBrightness(val);
        return;
    }

    // Lệnh cho từng đèn theo số: 1..4
    char firstChar = lowerCmd.charAt(0);
    if (firstChar >= '1' && firstChar <= '4') {
        int idx = firstChar - '1';

        // Nếu chỉ gõ "1", "2", "3", "4", "5" -> Đảo trạng thái (toggle)
        if (lowerCmd.length() == 1) {
            toggleLed(idx);
            return;
        }

        String sub = lowerCmd.substring(1);
        sub.trim();

        if (sub == "on" || sub == "bat") {
            setPower(idx, true);
        } else if (sub == "off" || sub == "tat") {
            setPower(idx, false);
        } else if (sub == "toggle" || sub == "tog") {
            toggleLed(idx);
        } else if (sub == "+" || sub == "up") {
            stepBrightness(idx, 10);
        } else if (sub == "-" || sub == "down") {
            stepBrightness(idx, -10);
        } else if (sub.startsWith("bri ") || sub.startsWith("b ")) {
            int spaceIdx = sub.lastIndexOf(' ');
            int val = sub.substring(spaceIdx + 1).toInt();
            setBrightness(idx, val);
        } else if (sub.endsWith("%")) {
            int val = sub.substring(0, sub.length() - 1).toInt();
            setBrightness(idx, val);
        } else if (sub.toInt() > 0 || sub == "0") {
            // Hỗ trợ gõ dạng: "1 50" -> đặt đèn 1 độ sáng 50%
            int val = sub.toInt();
            setBrightness(idx, val);
        } else {
            Serial.printf("[ERROR] Cú pháp không hợp lệ cho đèn %d! Gõ 'help' để xem hướng dẫn.\n", idx + 1);
        }
        return;
    }

    Serial.println("[ERROR] Lệnh không nhận diện được! Gõ 'help' để xem danh sách lệnh.");
}

// ====================================================================================================
// 6. KHỞI TẠO VÀ CHẠY
// ====================================================================================================
void setup() {
    Serial.begin(115200);

    // 1. Khởi tạo xung PWM phần cứng cho 5 chân GPIO
    initPWM();

    // 2. CẮM ĐIỆN LÀ BẬT SÁNG TOÀN BỘ 5 ĐÈN Ở ĐỘ SÁNG 100% NGAY LẬP TỨC
    for (int i = 0; i < NUM_LEDS; i++) {
        ledPower[i]      = true;
        ledBrightness[i] = 100;
        applyLedOutput(i);
    }

    delay(500);
    Serial.println("\n=======================================================");
    Serial.println("  TEST PHẦN CỨNG 4 KÊNH ĐÈN MOSFET - ESP32-S3 N16R8");
    Serial.println("  CẮM ĐIỆN -> CẢ 4 ĐÈN ĐÃ TỰ ĐỘNG BẬT SÁNG 100%!");
    Serial.println("=======================================================");
    Serial.println("Danh sách chân PWM đang hoạt động:");
    for (int i = 0; i < NUM_LEDS; i++) {
        Serial.printf("  - Đèn %d: GPIO %-2d | Kênh PWM %d | Độ sáng: 100%% | ID: %s\n", 
                      i + 1, LED_PINS[i], PWM_CHANNELS[i], DEVICE_IDS[i]);
    }
    Serial.println("-------------------------------------------------------");
    Serial.println("Gợi ý các lệnh qua Serial Monitor (Baudrate 115200):");
    Serial.println("  - Tăng / Giảm độ sáng : '1 +', '1 -', 'all +', 'all -'");
    Serial.println("  - Đặt độ sáng trực tiếp: '1 bri 50', '2 80%', 'all bri 40'");
    Serial.println("  - Bật / Tắt           : '1 off', '1 on', 'all off', 'all on'");
    Serial.println("  - Chạy hiệu ứng mờ dần: 'fade'");
    Serial.println("=======================================================\n");
}

void loop() {
    // 1. Chạy bước thở/mờ dần nếu bật chế độ fade tự động
    runAutoFadeStep();

    // 2. Nhận và xử lý lệnh từ Serial Monitor
    handleSerialCommands();
}
