/*
 * ====================================================================================================
 * DỰ ÁN: HESTA / SYNA SMART HOME - BỘ ĐIỀU KHIỂN 4 KÊNH ĐÈN MOSFET + CỔNG TRƯỢT & CỬA CUỐN
 * THIẾT KẾ DÀNH CHO: ESP32 / ESP32-S3 (TƯƠNG THÍCH HOÀN TOÀN ESP32-S3 N16R8)
 * ====================================================================================================
 * 
 * 1. THÔNG TIN HỆ THỐNG (ĐỊNH DANH BACKEND HESTA):
 * ----------------------------------------------------------------------------------------------------
 * - NODE ID      : 32442d7f-dfcf-4b2f-a380-adba32876cec
 * - DEVICE LED 1 : 19f79c42-afc1-4796-a48c-8d076ea45b65 (Đèn 1 - GPIO 4)
 * - DEVICE LED 2 : 98ae5452-250c-44c6-8b1a-fac08b2fc716 (Đèn 2 - GPIO 5)
 * - DEVICE LED 3 : c8186ca5-ff09-486c-bc56-ac75ef85e6d9 (Đèn 3 - GPIO 7)
 * - DEVICE LED 4 : c968b5c4-80d1-4aec-abef-cc52876a6da5 (Đèn 4 - GPIO 8)
 * - DEVICE CỔNG  : 2d566d0a-f7d6-4fac-a0bf-18912da4ab28 (Cổng trượt/cổng lùa - L298N Motor A)
 * - DEVICE CỬA CUỐN: c77b64a6-e163-44eb-83d2-431de5eb8f43 (Cửa cuốn - L298N Motor B)
 * 
 * 2. NGUYÊN LÝ HOẠT ĐỘNG & SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG:
 * ----------------------------------------------------------------------------------------------------
 * - 4 Kênh LED: Điều khiển đóng/ngắt và điều chỉnh độ sáng (PWM 5000Hz) qua MOSFET (GPIO 4, 5, 7, 8).
 * - Cổng trượt (Sliding Gate): Điều khiển Motor A qua L298N (IN1=GPIO 10, IN2=GPIO 11, ENA=GPIO 12).
 *   Kèm 2 công tắc hành trình: Mở=GPIO 13, Đóng=GPIO 14 (INPUT_PULLUP, chạm=LOW).
 * - Cửa cuốn (Rolling Door): Điều khiển Motor B qua L298N (IN3=GPIO 15, IN4=GPIO 16, ENB=GPIO 17).
 *   Kèm 2 công tắc hành trình: Cuộn lên=GPIO 18, Hạ xuống=GPIO 21 (INPUT_PULLUP, chạm=LOW).
 * 
 * 3. TÍNH NĂNG CHÍNH:
 * ----------------------------------------------------------------------------------------------------
 * - Điều khiển 4 đèn độc lập hoặc đồng loạt, Smooth Fading, nhớ trạng thái vào Flash NVS.
 * - Điều khiển Cổng trượt và Cửa cuốn (OPEN, CLOSE, STOP) với PWM điều tốc và tự ngắt hành trình/timeout.
 * - BLE Provisioning chuẩn SYNA Mobile App.
 * - Kết nối Wi-Fi & MQTT tự phục hồi khi rớt mạng, dò mDNS broker.
 * - Phản hồi ACK, State, Telemetry đầy đủ về Backend Hesta.
 * - Giao diện dòng lệnh Serial Monitor (115200 baud).
 * ====================================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ====================================================================================================
// 1. CẤU HÌNH NODE & DEVICE IDS (HESTA BACKEND / DATABASE)
// ====================================================================================================
#define NODE_ID "32442d7f-dfcf-4b2f-a380-adba32876cec"

#define NUM_LEDS 4

#define DEVICE_ID_LED1 "19f79c42-afc1-4796-a48c-8d076ea45b65"
#define DEVICE_ID_LED2 "98ae5452-250c-44c6-8b1a-fac08b2fc716"
#define DEVICE_ID_LED3 "c8186ca5-ff09-486c-bc56-ac75ef85e6d9"
#define DEVICE_ID_LED4 "c968b5c4-80d1-4aec-abef-cc52876a6da5"

// Thiết bị Cổng trượt & Cửa cuốn
#define GATE_DEVICE_ID          "2d566d0a-f7d6-4fac-a0bf-18912da4ab28"
#define ROLLING_DOOR_DEVICE_ID  "c77b64a6-e163-44eb-83d2-431de5eb8f43"

#define DEVICE_TYPE_LIGHT       "LIGHT"
#define DEVICE_TYPE_GATE        "CURTAIN"
#define DEVICE_TYPE_ROLLING_DOOR "ROLLING_DOOR"

// ====================================================================================================
// 2. CHẾ ĐỘ TEST OFFLINE (SERIAL TEST MODE) & CẤU HÌNH PHẦN CỨNG
// ====================================================================================================
// Đặt true : BỎ QUA kết nối Wi-Fi, MQTT, BLE. Dành riêng để test 4 đèn, PWM, MOSFET, Cổng/Cửa qua Serial.
//            Vòng lặp loop() chạy mượt mà tức thì, không bị đứng/lag bởi socket timeout 15s.
// Đặt false: Chạy đầy đủ hệ thống thật (BLE Provisioning -> Wi-Fi -> mDNS -> MQTT Broker).
#define OFFLINE_TEST_MODE false
bool offlineTestMode = OFFLINE_TEST_MODE;

// --- 2.1 4 KÊNH ĐÈN LED (MOSFET PWM) ---
#define PIN_LED1 4
#define PIN_LED2 5
#define PIN_LED3 7
#define PIN_LED4 8

bool mosfetActiveHigh        = true;

#define PWM_FREQ       500
#define PWM_RESOLUTION 8
#define PWM_MAX_DUTY   255

bool enableSmoothFading      = true;
#define FADE_SPEED           0.18f

// --- 2.2 MOTOR A (CỔNG TRƯỢT / CỔNG LÙA - SLIDING GATE - L298N) ---
#define PIN_GATE_IN1             10
#define PIN_GATE_IN2             11
#define PIN_GATE_ENA             12   // Tốc độ PWM Motor A
#define DEFAULT_GATE_OPEN_MS     2000 // Thời gian mở cổng mặc định: 2000ms (2 giây)
#define DEFAULT_GATE_CLOSE_MS    1900 // Thời gian đóng cổng mặc định: 1900ms (1.9 giây)
#define DEFAULT_GATE_TRAVEL_MS   DEFAULT_GATE_OPEN_MS // Tương thích ngược
#define DEFAULT_GATE_MS_PER_TURN 1000  // Thời gian 1 vòng quay motor cổng (~120 RPM: 500ms/vòng)

// --- 2.3 MOTOR B (CỬA CUỐN / RÈM - ROLLING DOOR / CURTAIN - L298N) ---
#define PIN_DOOR_IN3             15
#define PIN_DOOR_IN4             16
#define PIN_DOOR_ENB             17   // Tốc độ PWM Motor B
#define DEFAULT_DOOR_OPEN_MS     2800 // Thời gian cuộn lên / mở rèm mặc định: 2000ms (2 giây)
#define DEFAULT_DOOR_CLOSE_MS    1400 // Thời gian hạ xuống / đóng rèm mặc định: 1900ms (1.9 giây)
#define DEFAULT_DOOR_TRAVEL_MS   DEFAULT_DOOR_OPEN_MS // Tương thích ngược
#define DEFAULT_DOOR_MS_PER_TURN 500  // Thời gian 1 vòng quay motor cửa cuốn (~120 RPM: 500ms/vòng)

#define MOTOR_SPEED_DEFAULT      120 // Tốc độ PWM mặc định (0 - 255)
#define MOTOR_MAX_RUN_TIME_MS    10000 // Giới hạn an toàn tối đa 60 giây nếu đặt thời gian quá lớn

// ====================================================================================================
// 3. CẤU HÌNH WIFI & MQTT BROKER FALLBACK
// ====================================================================================================
#define DEFAULT_WIFI_SSID     "Dung Home T3"
#define DEFAULT_WIFI_PASSWORD "123lyvanphuc"

#define FALLBACK_MQTT_IP      "192.168.88.120"
#define DEFAULT_MQTT_PORT     1883
#define MQTT_USER             ""
#define MQTT_PASSWORD         ""

#define MQTT_DISCOVERY_SERVICE  "syna-mqtt"
#define MQTT_DISCOVERY_PROTOCOL "tcp"

#define FIRMWARE_VERSION        "2.0.0"
#define DEVICE_HUB_NAME         "SYNA-LED-GATE-DOOR"

// Topics chuyên biệt cho Cổng và Cửa cuốn
const String TOPIC_GATE_COMMAND    = "hesta/nodes/" NODE_ID "/devices/" GATE_DEVICE_ID "/command";
const String TOPIC_GATE_STATE      = "hesta/nodes/" NODE_ID "/devices/" GATE_DEVICE_ID "/state";
const String TOPIC_GATE_ACK        = "hesta/nodes/" NODE_ID "/devices/" GATE_DEVICE_ID "/ack";
const String TOPIC_GATE_TELEMETRY  = "hesta/nodes/" NODE_ID "/devices/" GATE_DEVICE_ID "/telemetry";

const String TOPIC_DOOR_COMMAND    = "hesta/nodes/" NODE_ID "/devices/" ROLLING_DOOR_DEVICE_ID "/command";
const String TOPIC_DOOR_STATE      = "hesta/nodes/" NODE_ID "/devices/" ROLLING_DOOR_DEVICE_ID "/state";
const String TOPIC_DOOR_ACK        = "hesta/nodes/" NODE_ID "/devices/" ROLLING_DOOR_DEVICE_ID "/ack";
const String TOPIC_DOOR_TELEMETRY  = "hesta/nodes/" NODE_ID "/devices/" ROLLING_DOOR_DEVICE_ID "/telemetry";

// ====================================================================================================
// 4. CẤU HÌNH BLE PROVISIONING (CHUẨN HỆ SINH THÁI SYNA MOBILE APP)
// ====================================================================================================
#define BLE_SERVICE_UUID   "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID "12345678-1234-1234-1234-123456789001"

// ====================================================================================================
// 5. CẤU TRÚC QUẢN LÝ TỪNG KÊNH ĐÈN LED & TRẠNG THÁI MOTOR
// ====================================================================================================
struct LedChannel {
    const char* deviceId;     // UUID định danh trên backend
    const char* label;        // Tên hiển thị (VD: "Đèn Bàn Ăn", "LED 1")
    uint8_t pin;              // Chân GPIO
    uint8_t pwmChannel;       // Kênh PWM phần cứng (0 - 7 trên ESP32)
    bool power;               // Trạng thái BẬT (true) / TẮT (false)
    uint8_t brightness;       // Độ sáng người dùng đặt: 0 - 100%
    uint8_t lastBrightness;   // Độ sáng nhớ trước khi tắt (để khi bật lại khôi phục đúng)
    float currentDuty;        // Giá trị PWM hiện tại (0.0 - 255.0) đang xuất ra chân
    float targetDuty;         // Giá trị PWM đích cần đạt tới
};

LedChannel leds[NUM_LEDS] = {
    {DEVICE_ID_LED1, "LED_1", PIN_LED1, 0, false, 100, 100, 0.0f, 0.0f},
    {DEVICE_ID_LED2, "LED_2", PIN_LED2, 1, false, 100, 100, 0.0f, 0.0f},
    {DEVICE_ID_LED3, "LED_3", PIN_LED3, 2, false, 100, 100, 0.0f, 0.0f},
    {DEVICE_ID_LED4, "LED_4", PIN_LED4, 3, false, 100, 100, 0.0f, 0.0f}
};

// --- TRẠNG THÁI CỔNG TRƯỢT VÀ CỬA CUỐN ---
enum MotionState {
    STATE_STOPPED,
    STATE_OPENING,
    STATE_CLOSING,
    STATE_OPEN,
    STATE_CLOSED
};

MotionState gateState = STATE_STOPPED;
MotionState doorState = STATE_STOPPED;

unsigned long gateRunStartTime = 0;
unsigned long doorRunStartTime = 0;

// --- THÔNG SỐ ĐIỀU KHIỂN THEO THỜI GIAN & VÒNG QUAY (PERSISTED IN NVS) ---
// Motor A: Cổng trượt (Tách riêng thời gian mở và đóng)
uint32_t gateOpenTimeMs            = DEFAULT_GATE_OPEN_MS;
uint32_t gateCloseTimeMs           = DEFAULT_GATE_CLOSE_MS;
uint32_t gateTravelTimeMs          = DEFAULT_GATE_OPEN_MS; // Giữ để tương thích ngược
uint16_t gateMsPerTurn             = DEFAULT_GATE_MS_PER_TURN;
uint8_t  gateSpeed                 = MOTOR_SPEED_DEFAULT;
uint32_t currentGateTargetDuration = DEFAULT_GATE_OPEN_MS;

// Motor B: Cửa cuốn / Rèm (Tách riêng thời gian cuộn lên và hạ xuống)
uint32_t doorOpenTimeMs            = DEFAULT_DOOR_OPEN_MS;
uint32_t doorCloseTimeMs           = DEFAULT_DOOR_CLOSE_MS;
uint32_t doorTravelTimeMs          = DEFAULT_DOOR_OPEN_MS; // Giữ để tương thích ngược
uint16_t doorMsPerTurn             = DEFAULT_DOOR_MS_PER_TURN;
uint8_t  doorSpeed                 = MOTOR_SPEED_DEFAULT;
uint32_t currentDoorTargetDuration = DEFAULT_DOOR_OPEN_MS;

// ====================================================================================================
// 6. BIẾN TOÀN CỤC VÀ ĐỐI TƯỢNG HỆ THỐNG
// ====================================================================================================
Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

BLEServer* bleServer = nullptr;
BLECharacteristic* commCharacteristic = nullptr;
bool bleClientConnected = false;
volatile bool shouldConnectWiFi = false;

String wifiSSID = DEFAULT_WIFI_SSID;
String wifiPassword = DEFAULT_WIFI_PASSWORD;

IPAddress mqttBrokerIP;
uint16_t mqttBrokerPort = DEFAULT_MQTT_PORT;
bool mqttBrokerFound = false;

unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastTelemetryPublish = 0;
unsigned long lastFadingUpdate = 0;

// Chu kỳ gửi Telemetry định kỳ (mỗi 30 giây)
const unsigned long TELEMETRY_INTERVAL_MS = 30000;

// ====================================================================================================
// 7. NGUYÊN MẪU HÀM (FORWARD DECLARATIONS)
// ====================================================================================================
void initPWM();
void writeHardwarePWM(uint8_t index, uint8_t duty);
void updateLedTargetDuty(uint8_t index);
void setLedPower(uint8_t index, bool power, const char* commandId = nullptr);
void setLedBrightness(uint8_t index, int brightnessPercent, const char* commandId = nullptr);
void toggleLed(uint8_t index, const char* commandId = nullptr);
void stepBrightness(uint8_t index, int delta, const char* commandId = nullptr);
void setAllPower(bool power, const char* commandId = nullptr);
void setAllBrightness(int brightnessPercent, const char* commandId = nullptr);
void stepAllBrightness(int delta, const char* commandId = nullptr);

int findLedIndexByDeviceId(const String& deviceId);
void saveLedStateToNVS(uint8_t index);
void loadAllStatesFromNVS();

const char* stateToString(MotionState state);
void initMotorHardware();
void setGateSpeed(int speed);
void setDoorSpeed(int speed);
void gateStop(const char* commandId = nullptr, bool publishMqtt = true);
void gateOpen(uint32_t durationMs = 0, const char* commandId = nullptr);
void gateClose(uint32_t durationMs = 0, const char* commandId = nullptr);
void setGateOpenDuration(uint32_t durationMs);
void setGateCloseDuration(uint32_t durationMs);
void setGateDuration(uint32_t durationMs);
void setGateTurns(float openTurns, float closeTurns = 0.0f, uint16_t msPerTurn = 0);
void publishGateState(const char* commandId = nullptr);
void publishGateTelemetry();

void doorStop(const char* commandId = nullptr, bool publishMqtt = true);
void doorOpen(uint32_t durationMs = 0, const char* commandId = nullptr);
void doorClose(uint32_t durationMs = 0, const char* commandId = nullptr);
void setDoorOpenDuration(uint32_t durationMs);
void setDoorCloseDuration(uint32_t durationMs);
void setDoorDuration(uint32_t durationMs);
void setDoorTurns(float openTurns, float closeTurns = 0.0f, uint16_t msPerTurn = 0);
void publishDoorState(const char* commandId = nullptr);
void publishDoorTelemetry();

void updateMotorTimers();
void loadMotorConfigFromNVS();
void saveMotorConfigToNVS();

void startBLE();
void notifyBleStatus(const char* status, const char* key1 = nullptr, const char* val1 = nullptr, const char* key2 = nullptr, const char* val2 = nullptr);

bool connectWiFi();
bool discoverMQTTBroker();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void processCommandPayload(const String& targetDeviceId, const char* payloadStr);

void publishDeviceState(uint8_t index);
void publishDeviceAck(uint8_t index, const char* commandId, const char* statusMsg = "SUCCESS");
void publishDeviceTelemetry(uint8_t index);
void publishDeviceStatus(uint8_t index, const char* status);
void publishAllTelemetries();
void publishNodeDiscovery();

void handleSerialCommands();
void printHelp();
void printStatus();

// ====================================================================================================
// 8. KHỞI TẠO VÀ XUẤT PWM CHO MOSFET (TƯƠNG THÍCH CORE 2.x VÀ 3.x)
// ====================================================================================================
void initPWM() {
    for (int i = 0; i < NUM_LEDS; i++) {
        pinMode(leds[i].pin, OUTPUT);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        // Arduino ESP32 Core 3.x: Sử dụng trực tiếp chân pin
        ledcAttach(leds[i].pin, PWM_FREQ, PWM_RESOLUTION);
#else
        // Arduino ESP32 Core 2.x: Sử dụng kênh PWM ledcSetup
        ledcSetup(leds[i].pwmChannel, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(leds[i].pin, leds[i].pwmChannel);
#endif

        writeHardwarePWM(i, 0);
    }
}

void writeHardwarePWM(uint8_t index, uint8_t duty) {
    if (index >= NUM_LEDS) return;

    uint8_t physicalDuty = duty;
    if (!mosfetActiveHigh) {
        physicalDuty = PWM_MAX_DUTY - duty;
    }

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(leds[index].pin, physicalDuty);
#else
    ledcWrite(leds[index].pwmChannel, physicalDuty);
#endif
}

/**
 * Tính toán duty mục tiêu dựa trên power và brightness
 */
void updateLedTargetDuty(uint8_t index) {
    if (index >= NUM_LEDS) return;

    if (!leds[index].power || leds[index].brightness == 0) {
        leds[index].targetDuty = 0.0f;
    } else {
        // Map 0% -> 100% sang 0 -> 255
        leds[index].targetDuty = (float)((uint16_t)leds[index].brightness * PWM_MAX_DUTY) / 100.0f;
    }

    if (!enableSmoothFading) {
        leds[index].currentDuty = leds[index].targetDuty;
        writeHardwarePWM(index, (uint8_t)round(leds[index].currentDuty));
    }
}

/**
 * Cập nhật hiệu ứng Smooth Fading liên tục trong loop()
 */
void updateSmoothFading() {
    if (!enableSmoothFading) return;

    for (int i = 0; i < NUM_LEDS; i++) {
        if (fabs(leds[i].currentDuty - leds[i].targetDuty) > 0.5f) {
            leds[i].currentDuty += (leds[i].targetDuty - leds[i].currentDuty) * FADE_SPEED;
            writeHardwarePWM(i, (uint8_t)round(leds[i].currentDuty));
        } else if (leds[i].currentDuty != leds[i].targetDuty) {
            leds[i].currentDuty = leds[i].targetDuty;
            writeHardwarePWM(i, (uint8_t)round(leds[i].currentDuty));
        }
    }
}

// ====================================================================================================
// 9. LOGIC ĐIỀU KHIỂN ĐÈN (BẬT, TẮT, ĐỘ SÁNG)
// ====================================================================================================

/**
 * Bật hoặc Tắt đèn tại chỉ mục index (0..4)
 */
void setLedPower(uint8_t index, bool power, const char* commandId) {
    if (index >= NUM_LEDS) return;

    leds[index].power = power;

    if (power) {
        // Nếu trước đó độ sáng = 0 thì khôi phục độ sáng đã nhớ hoặc 100%
        if (leds[index].brightness == 0) {
            leds[index].brightness = (leds[index].lastBrightness > 0) ? leds[index].lastBrightness : 100;
        }
    } else {
        // Khi tắt, lưu lại độ sáng hiện tại để khôi phục lần sau
        if (leds[index].brightness > 0) {
            leds[index].lastBrightness = leds[index].brightness;
        }
    }

    updateLedTargetDuty(index);
    saveLedStateToNVS(index);

    Serial.printf("[LED %d - %s] %s | Do sang: %d%%\n", 
                  index + 1, leds[index].deviceId, 
                  leds[index].power ? "BAT (ON)" : "TAT (OFF)", 
                  leds[index].brightness);

    // Báo State, ACK và Telemetry qua MQTT
    publishDeviceState(index);
    if (commandId != nullptr && strlen(commandId) > 0) {
        publishDeviceAck(index, commandId, "SUCCESS");
    }
    publishDeviceTelemetry(index);
}

/**
 * Đặt độ sáng tuyệt đối (0% - 100%)
 */
void setLedBrightness(uint8_t index, int brightnessPercent, const char* commandId) {
    if (index >= NUM_LEDS) return;

    brightnessPercent = constrain(brightnessPercent, 0, 100);
    leds[index].brightness = (uint8_t)brightnessPercent;

    if (brightnessPercent > 0) {
        leds[index].power = true;
        leds[index].lastBrightness = (uint8_t)brightnessPercent;
    } else {
        leds[index].power = false;
    }

    updateLedTargetDuty(index);
    saveLedStateToNVS(index);

    Serial.printf("[LED %d - %s] Set Do sang: %d%% (%s)\n", 
                  index + 1, leds[index].deviceId, 
                  leds[index].brightness,
                  leds[index].power ? "ON" : "OFF");

    publishDeviceState(index);
    if (commandId != nullptr && strlen(commandId) > 0) {
        publishDeviceAck(index, commandId, "SUCCESS");
    }
    publishDeviceTelemetry(index);
}

/**
 * Đảo trạng thái Bật / Tắt (Toggle)
 */
void toggleLed(uint8_t index, const char* commandId) {
    if (index >= NUM_LEDS) return;
    setLedPower(index, !leds[index].power, commandId);
}

/**
 * Tăng / Giảm độ sáng tương đối theo bước (VD: +10% hoặc -10%)
 */
void stepBrightness(uint8_t index, int delta, const char* commandId) {
    if (index >= NUM_LEDS) return;

    int currentBri = 0;
    if (leds[index].power) {
        currentBri = (int)leds[index].brightness;
    } else {
        // Đèn đang tắt: nếu delta > 0, lấy độ sáng nhớ trước đó hoặc tối thiểu 10%
        if (delta > 0) {
            currentBri = (leds[index].lastBrightness > 0) ? (int)leds[index].lastBrightness : 10;
        } else {
            currentBri = 0;
        }
    }

    int newBri = currentBri + delta;
    newBri = constrain(newBri, 0, 100);

    Serial.printf("[LED %d] Step brightness: %d%% -> %d%% (delta: %+d)\n", 
                  index + 1, currentBri, newBri, delta);

    setLedBrightness(index, newBri, commandId);
}

/**
 * Tăng / Giảm độ sáng cho tất cả các đèn
 */
void stepAllBrightness(int delta, const char* commandId) {
    Serial.printf("[LED ALL] Step tat ca %d den: delta = %+d%%\n", NUM_LEDS, delta);
    for (int i = 0; i < NUM_LEDS; i++) {
        stepBrightness(i, delta, commandId);
    }
}

/**
 * Bật / Tắt tất cả các đèn
 */
void setAllPower(bool power, const char* commandId) {
    Serial.printf("[LED ALL] Chuyen tat ca %d den sang trang thai: %s\n", NUM_LEDS, power ? "ON" : "OFF");
    for (int i = 0; i < NUM_LEDS; i++) {
        setLedPower(i, power, commandId);
    }
}

/**
 * Đặt độ sáng cho tất cả các đèn
 */
void setAllBrightness(int brightnessPercent, const char* commandId) {
    Serial.printf("[LED ALL] Dat do sang tat ca %d den thanh: %d%%\n", NUM_LEDS, brightnessPercent);
    for (int i = 0; i < NUM_LEDS; i++) {
        setLedBrightness(i, brightnessPercent, commandId);
    }
}

/**
 * Tìm chỉ mục mảng leds[] dựa theo Device ID UUID, Label hoặc Bí danh
 */
int findLedIndexByDeviceId(const String& deviceId) {
    String cleanId = deviceId;
    cleanId.trim();

    // 1. So sánh trực tiếp UUID hoặc Label ("LED_1", "LED_2", ...)
    for (int i = 0; i < NUM_LEDS; i++) {
        if (cleanId.equalsIgnoreCase(leds[i].deviceId) || cleanId.equalsIgnoreCase(leds[i].label)) {
            return i;
        }
    }

    // 2. So sánh bí danh phụ: "led1", "led_1", "den1", "light1", "1", "0"
    String lowerId = cleanId;
    lowerId.toLowerCase();
    if (lowerId.startsWith("led_") || lowerId.startsWith("led") || 
        lowerId.startsWith("den_") || lowerId.startsWith("den") || 
        lowerId.startsWith("light_") || lowerId.startsWith("light")) {
        char lastC = lowerId.charAt(lowerId.length() - 1);
        if (lastC >= '1' && lastC <= '4') {
            return (lastC - '1');
        }
    }

    if (lowerId.length() == 1) {
        if (lowerId.charAt(0) >= '1' && lowerId.charAt(0) <= '4') {
            return (lowerId.charAt(0) - '1');
        }
        if (lowerId.charAt(0) >= '0' && lowerId.charAt(0) <= '3') {
            return (lowerId.charAt(0) - '0');
        }
    }

    return -1;
}

// ====================================================================================================
// 9b. ĐIỀU KHIỂN CỔNG TRƯỢT (SLIDING GATE) & CỬA CUỐN (ROLLING DOOR)
// ====================================================================================================
const char* stateToString(MotionState state) {
    switch (state) {
        case STATE_STOPPED: return "STOPPED";
        case STATE_OPENING: return "OPENING";
        case STATE_CLOSING: return "CLOSING";
        case STATE_OPEN:    return "OPEN";
        case STATE_CLOSED:  return "CLOSED";
        default:            return "UNKNOWN";
    }
}

void initMotorHardware() {
    pinMode(PIN_GATE_IN1, OUTPUT);
    pinMode(PIN_GATE_IN2, OUTPUT);
    digitalWrite(PIN_GATE_IN1, LOW);
    digitalWrite(PIN_GATE_IN2, LOW);

    pinMode(PIN_DOOR_IN3, OUTPUT);
    pinMode(PIN_DOOR_IN4, OUTPUT);
    digitalWrite(PIN_DOOR_IN3, LOW);
    digitalWrite(PIN_DOOR_IN4, LOW);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_GATE_ENA, 1000, 8);
    ledcAttach(PIN_DOOR_ENB, 1000, 8);
#else
    ledcSetup(4, 1000, 8);
    ledcAttachPin(PIN_GATE_ENA, 4);
    ledcSetup(5, 1000, 8);
    ledcAttachPin(PIN_DOOR_ENB, 5);
#endif

    setGateSpeed(0);
    setDoorSpeed(0);

    Serial.printf("[MOTOR INIT] Da khoi tao Cong truot (IN1=%d, IN2=%d, ENA=%d) & Cua cuon (IN3=%d, IN4=%d, ENB=%d)\n",
                  PIN_GATE_IN1, PIN_GATE_IN2, PIN_GATE_ENA, PIN_DOOR_IN3, PIN_DOOR_IN4, PIN_DOOR_ENB);
    Serial.println("[MOTOR INIT] Che do: DUNG THOI GIAN / VONG QUAY (Khong su dung cong tac hanh trinh)");
}

void setGateSpeed(int speed) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_GATE_ENA, speed);
#else
    ledcWrite(4, speed);
#endif
}

void setDoorSpeed(int speed) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_DOOR_ENB, speed);
#else
    ledcWrite(5, speed);
#endif
}

void setGateOpenDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    gateOpenTimeMs = durationMs;
    gateTravelTimeMs = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[GATE CONFIG] Da cai dat thoi gian MO cong truot: %u ms (%.2f giay)\n", gateOpenTimeMs, (float)gateOpenTimeMs / 1000.0f);
}

void setGateCloseDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    gateCloseTimeMs = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[GATE CONFIG] Da cai dat thoi gian DONG cong truot: %u ms (%.2f giay)\n", gateCloseTimeMs, (float)gateCloseTimeMs / 1000.0f);
}

void setGateDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    gateOpenTimeMs = durationMs;
    gateCloseTimeMs = durationMs;
    gateTravelTimeMs = durationMs;
    currentGateTargetDuration = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[GATE CONFIG] Da cai dat ca Dong & Mo cong truot: %u ms (%.2f giay)\n", durationMs, (float)durationMs / 1000.0f);
}

void setGateTurns(float openTurns, float closeTurns, uint16_t msPerTurn) {
    if (msPerTurn > 0) gateMsPerTurn = msPerTurn;
    if (openTurns <= 0) openTurns = 1.0f;
    if (closeTurns <= 0) closeTurns = openTurns;

    gateOpenTimeMs = (uint32_t)(openTurns * gateMsPerTurn);
    gateCloseTimeMs = (uint32_t)(closeTurns * gateMsPerTurn);
    gateTravelTimeMs = gateOpenTimeMs;
    saveMotorConfigToNVS();
    Serial.printf("[GATE CONFIG] Da cai dat so vong quay cong: Mo=%.2f vong (%u ms), Dong=%.2f vong (%u ms) | ms_per_turn=%u\n",
                  openTurns, gateOpenTimeMs, closeTurns, gateCloseTimeMs, gateMsPerTurn);
}

void gateStop(const char* commandId, bool publishMqtt) {
    digitalWrite(PIN_GATE_IN1, LOW);
    digitalWrite(PIN_GATE_IN2, LOW);
    setGateSpeed(0);

    gateState = STATE_STOPPED;
    gateRunStartTime = 0;

    Serial.println("[GATE] MOTOR A STOPPED");

    if (publishMqtt) {
        publishGateState(commandId);
    }
}

void gateOpen(uint32_t durationMs, const char* commandId) {
    uint32_t targetMs = (durationMs > 0) ? durationMs : gateOpenTimeMs;
    if (targetMs > MOTOR_MAX_RUN_TIME_MS) targetMs = MOTOR_MAX_RUN_TIME_MS;
    currentGateTargetDuration = targetMs;

    Serial.printf("[GATE] DANG MO CONG TRUOT... Thoi gian chay: %u ms (%.2f s) | Toc do PWM: %d\n",
                  currentGateTargetDuration, (float)currentGateTargetDuration / 1000.0f, gateSpeed);

    digitalWrite(PIN_GATE_IN1, HIGH);
    digitalWrite(PIN_GATE_IN2, LOW);
    setGateSpeed(gateSpeed);

    gateState = STATE_OPENING;
    gateRunStartTime = millis();

    publishGateState(commandId);
}

void gateClose(uint32_t durationMs, const char* commandId) {
    uint32_t targetMs = (durationMs > 0) ? durationMs : gateCloseTimeMs;
    if (targetMs > MOTOR_MAX_RUN_TIME_MS) targetMs = MOTOR_MAX_RUN_TIME_MS;
    currentGateTargetDuration = targetMs;

    Serial.printf("[GATE] DANG DONG CONG TRUOT... Thoi gian chay: %u ms (%.2f s) | Toc do PWM: %d\n",
                  currentGateTargetDuration, (float)currentGateTargetDuration / 1000.0f, gateSpeed);

    digitalWrite(PIN_GATE_IN1, LOW);
    digitalWrite(PIN_GATE_IN2, HIGH);
    setGateSpeed(gateSpeed);

    gateState = STATE_CLOSING;
    gateRunStartTime = millis();

    publishGateState(commandId);
}

void publishGateState(const char* commandId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]           = NODE_ID;
    doc["deviceId"]         = GATE_DEVICE_ID;
    doc["device"]           = "gate";
    doc["state"]            = stateToString(gateState);
    doc["openDurationMs"]   = gateOpenTimeMs;
    doc["closeDurationMs"]  = gateCloseTimeMs;
    doc["durationMs"]       = gateOpenTimeMs;
    doc["targetDurationMs"] = currentGateTargetDuration;
    doc["openTurns"]        = (float)gateOpenTimeMs / (float)gateMsPerTurn;
    doc["closeTurns"]       = (float)gateCloseTimeMs / (float)gateMsPerTurn;
    doc["speed"]            = gateSpeed;

    JsonObject st = doc["state"].to<JsonObject>();
    st["state"]            = stateToString(gateState);
    st["openDurationMs"]   = gateOpenTimeMs;
    st["closeDurationMs"]  = gateCloseTimeMs;
    st["durationMs"]       = gateOpenTimeMs;
    st["targetDurationMs"] = currentGateTargetDuration;
    st["openTurns"]        = (float)gateOpenTimeMs / (float)gateMsPerTurn;
    st["closeTurns"]       = (float)gateCloseTimeMs / (float)gateMsPerTurn;
    st["speed"]            = gateSpeed;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_GATE_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> GATE]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = GATE_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["state"]            = stateToString(gateState);
        ackSt["openDurationMs"]   = gateOpenTimeMs;
        ackSt["closeDurationMs"]  = gateCloseTimeMs;
        ackSt["durationMs"]       = gateOpenTimeMs;
        ackSt["targetDurationMs"] = currentGateTargetDuration;
        ackSt["openTurns"]        = (float)gateOpenTimeMs / (float)gateMsPerTurn;
        ackSt["closeTurns"]       = (float)gateCloseTimeMs / (float)gateMsPerTurn;
        ackSt["speed"]            = gateSpeed;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_GATE_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> GATE]: %s\n", ackBuf);
    }
}

void publishGateTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]          = NODE_ID;
    doc["deviceId"]        = GATE_DEVICE_ID;
    doc["deviceType"]      = DEVICE_TYPE_GATE;
    doc["device"]          = "gate";
    doc["state"]           = stateToString(gateState);
    doc["openDurationMs"]  = gateOpenTimeMs;
    doc["closeDurationMs"] = gateCloseTimeMs;
    doc["speed"]           = gateSpeed;
    doc["uptime"]          = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_GATE_TELEMETRY.c_str(), buf, false);
}

void setDoorOpenDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    doorOpenTimeMs = durationMs;
    doorTravelTimeMs = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[DOOR CONFIG] Da cai dat thoi gian CUON LEN / MO cua cuon: %u ms (%.2f giay)\n", doorOpenTimeMs, (float)doorOpenTimeMs / 1000.0f);
}

void setDoorCloseDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    doorCloseTimeMs = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[DOOR CONFIG] Da cai dat thoi gian HA XUONG / DONG cua cuon: %u ms (%.2f giay)\n", doorCloseTimeMs, (float)doorCloseTimeMs / 1000.0f);
}

void setDoorDuration(uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > MOTOR_MAX_RUN_TIME_MS) durationMs = MOTOR_MAX_RUN_TIME_MS;
    doorOpenTimeMs = durationMs;
    doorCloseTimeMs = durationMs;
    doorTravelTimeMs = durationMs;
    currentDoorTargetDuration = durationMs;
    saveMotorConfigToNVS();
    Serial.printf("[DOOR CONFIG] Da cai dat ca Dong & Mo cua cuon: %u ms (%.2f giay)\n", durationMs, (float)durationMs / 1000.0f);
}

void setDoorTurns(float openTurns, float closeTurns, uint16_t msPerTurn) {
    if (msPerTurn > 0) doorMsPerTurn = msPerTurn;
    if (openTurns <= 0) openTurns = 1.0f;
    if (closeTurns <= 0) closeTurns = openTurns;

    doorOpenTimeMs = (uint32_t)(openTurns * doorMsPerTurn);
    doorCloseTimeMs = (uint32_t)(closeTurns * doorMsPerTurn);
    doorTravelTimeMs = doorOpenTimeMs;
    saveMotorConfigToNVS();
    Serial.printf("[DOOR CONFIG] Da cai dat so vong quay cua cuon: Mo=%.2f vong (%u ms), Dong=%.2f vong (%u ms) | ms_per_turn=%u\n",
                  openTurns, doorOpenTimeMs, closeTurns, doorCloseTimeMs, doorMsPerTurn);
}

void doorStop(const char* commandId, bool publishMqtt) {
    digitalWrite(PIN_DOOR_IN3, LOW);
    digitalWrite(PIN_DOOR_IN4, LOW);
    setDoorSpeed(0);

    doorState = STATE_STOPPED;
    doorRunStartTime = 0;

    Serial.println("[DOOR] MOTOR B (CUA CUON / REM) STOPPED");

    if (publishMqtt) {
        publishDoorState(commandId);
    }
}

void doorOpen(uint32_t durationMs, const char* commandId) {
    uint32_t targetMs = (durationMs > 0) ? durationMs : doorOpenTimeMs;
    if (targetMs > MOTOR_MAX_RUN_TIME_MS) targetMs = MOTOR_MAX_RUN_TIME_MS;
    currentDoorTargetDuration = targetMs;

    Serial.printf("[DOOR] DANG CUON CUA LEN / MO REM... Thoi gian chay: %u ms (%.2f s) | Toc do PWM: %d\n",
                  currentDoorTargetDuration, (float)currentDoorTargetDuration / 1000.0f, doorSpeed);

    digitalWrite(PIN_DOOR_IN3, HIGH);
    digitalWrite(PIN_DOOR_IN4, LOW);
    setDoorSpeed(doorSpeed);

    doorState = STATE_OPENING;
    doorRunStartTime = millis();

    publishDoorState(commandId);
}

void doorClose(uint32_t durationMs, const char* commandId) {
    uint32_t targetMs = (durationMs > 0) ? durationMs : doorCloseTimeMs;
    if (targetMs > MOTOR_MAX_RUN_TIME_MS) targetMs = MOTOR_MAX_RUN_TIME_MS;
    currentDoorTargetDuration = targetMs;

    Serial.printf("[DOOR] DANG HA CUA CUON / DONG REM... Thoi gian chay: %u ms (%.2f s) | Toc do PWM: %d\n",
                  currentDoorTargetDuration, (float)currentDoorTargetDuration / 1000.0f, doorSpeed);

    digitalWrite(PIN_DOOR_IN3, LOW);
    digitalWrite(PIN_DOOR_IN4, HIGH);
    setDoorSpeed(doorSpeed);

    doorState = STATE_CLOSING;
    doorRunStartTime = millis();

    publishDoorState(commandId);
}

void publishDoorState(const char* commandId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]           = NODE_ID;
    doc["deviceId"]         = ROLLING_DOOR_DEVICE_ID;
    doc["device"]           = "rolling_door";
    doc["state"]            = stateToString(doorState);
    doc["openDurationMs"]   = doorOpenTimeMs;
    doc["closeDurationMs"]  = doorCloseTimeMs;
    doc["durationMs"]       = doorOpenTimeMs;
    doc["targetDurationMs"] = currentDoorTargetDuration;
    doc["openTurns"]        = (float)doorOpenTimeMs / (float)doorMsPerTurn;
    doc["closeTurns"]       = (float)doorCloseTimeMs / (float)doorMsPerTurn;
    doc["speed"]            = doorSpeed;

    JsonObject st = doc["state"].to<JsonObject>();
    st["state"]            = stateToString(doorState);
    st["openDurationMs"]   = doorOpenTimeMs;
    st["closeDurationMs"]  = doorCloseTimeMs;
    st["durationMs"]       = doorOpenTimeMs;
    st["targetDurationMs"] = currentDoorTargetDuration;
    st["openTurns"]        = (float)doorOpenTimeMs / (float)doorMsPerTurn;
    st["closeTurns"]       = (float)doorCloseTimeMs / (float)doorMsPerTurn;
    st["speed"]            = doorSpeed;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_DOOR_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> ROLLING DOOR]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = ROLLING_DOOR_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["state"]            = stateToString(doorState);
        ackSt["openDurationMs"]   = doorOpenTimeMs;
        ackSt["closeDurationMs"]  = doorCloseTimeMs;
        ackSt["durationMs"]       = doorOpenTimeMs;
        ackSt["targetDurationMs"] = currentDoorTargetDuration;
        ackSt["openTurns"]        = (float)doorOpenTimeMs / (float)doorMsPerTurn;
        ackSt["closeTurns"]       = (float)doorCloseTimeMs / (float)doorMsPerTurn;
        ackSt["speed"]            = doorSpeed;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_DOOR_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> ROLLING DOOR]: %s\n", ackBuf);
    }
}

void publishDoorTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]          = NODE_ID;
    doc["deviceId"]        = ROLLING_DOOR_DEVICE_ID;
    doc["deviceType"]      = DEVICE_TYPE_ROLLING_DOOR;
    doc["device"]          = "rolling_door";
    doc["state"]           = stateToString(doorState);
    doc["openDurationMs"]  = doorOpenTimeMs;
    doc["closeDurationMs"] = doorCloseTimeMs;
    doc["speed"]           = doorSpeed;
    doc["uptime"]          = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_DOOR_TELEMETRY.c_str(), buf, false);
}

void updateMotorTimers() {
    unsigned long now = millis();

    // 1. Giám sát Cổng trượt (Sliding Gate) theo thời gian quay
    if (gateState == STATE_OPENING) {
        if (now - gateRunStartTime >= currentGateTargetDuration) {
            digitalWrite(PIN_GATE_IN1, LOW);
            digitalWrite(PIN_GATE_IN2, LOW);
            setGateSpeed(0);
            gateState = STATE_OPEN;
            Serial.printf("[GATE TIMER] >>> HOAN THANH MO CONG THEO THOI GIAN (%u ms) <<<\n", currentGateTargetDuration);
            publishGateState();
            publishGateTelemetry();
        }
    } else if (gateState == STATE_CLOSING) {
        if (now - gateRunStartTime >= currentGateTargetDuration) {
            digitalWrite(PIN_GATE_IN1, LOW);
            digitalWrite(PIN_GATE_IN2, LOW);
            setGateSpeed(0);
            gateState = STATE_CLOSED;
            Serial.printf("[GATE TIMER] >>> HOAN THANH DONG CONG THEO THOI GIAN (%u ms) <<<\n", currentGateTargetDuration);
            publishGateState();
            publishGateTelemetry();
        }
    }

    // 2. Giám sát Cửa cuốn (Rolling Door) theo thời gian quay
    if (doorState == STATE_OPENING) {
        if (now - doorRunStartTime >= currentDoorTargetDuration) {
            digitalWrite(PIN_DOOR_IN3, LOW);
            digitalWrite(PIN_DOOR_IN4, LOW);
            setDoorSpeed(0);
            doorState = STATE_OPEN;
            Serial.printf("[DOOR TIMER] >>> HOAN THANH CUON CUA LEN THEO THOI GIAN (%u ms) <<<\n", currentDoorTargetDuration);
            publishDoorState();
            publishDoorTelemetry();
        }
    } else if (doorState == STATE_CLOSING) {
        if (now - doorRunStartTime >= currentDoorTargetDuration) {
            digitalWrite(PIN_DOOR_IN3, LOW);
            digitalWrite(PIN_DOOR_IN4, LOW);
            setDoorSpeed(0);
            doorState = STATE_CLOSED;
            Serial.printf("[DOOR TIMER] >>> HOAN THANH HA CUA CUON THEO THOI GIAN (%u ms) <<<\n", currentDoorTargetDuration);
            publishDoorState();
            publishDoorTelemetry();
        }
    }
}

// ====================================================================================================
// 10. BỘ NHỚ LƯU TRỮ FLASH NVS (PREFERENCES)
// ====================================================================================================
void saveLedStateToNVS(uint8_t index) {
    if (index >= NUM_LEDS) return;

    char keyP[16];
    char keyB[16];
    char keyL[16];
    snprintf(keyP, sizeof(keyP), "p_%d", index);
    snprintf(keyB, sizeof(keyB), "b_%d", index);
    snprintf(keyL, sizeof(keyL), "l_%d", index);

    preferences.begin("led_state", false);
    preferences.putBool(keyP, leds[index].power);
    preferences.putUChar(keyB, leds[index].brightness);
    preferences.putUChar(keyL, leds[index].lastBrightness);
    preferences.end();
}

void loadAllStatesFromNVS() {
    preferences.begin("led_state", true);
    for (int i = 0; i < NUM_LEDS; i++) {
        char keyP[16];
        char keyB[16];
        char keyL[16];
        snprintf(keyP, sizeof(keyP), "p_%d", i);
        snprintf(keyB, sizeof(keyB), "b_%d", i);
        snprintf(keyL, sizeof(keyL), "l_%d", i);

        leds[i].power          = preferences.getBool(keyP, false);
        leds[i].brightness     = preferences.getUChar(keyB, 100);
        leds[i].lastBrightness = preferences.getUChar(keyL, 100);

        updateLedTargetDuty(i);
        // Khởi động ban đầu: gán ngay duty thực tế để tránh nhấp nháy
        leds[i].currentDuty = leds[i].targetDuty;
        writeHardwarePWM(i, (uint8_t)round(leds[i].currentDuty));
    }
    preferences.end();
    Serial.println("[NVS] Da khoi phuc trang thai 5 den tu bo nho Flash NVS.");
}

void loadNetworkConfigFromNVS() {
    preferences.begin("syna_net", true);
    wifiSSID = preferences.getString("ssid", DEFAULT_WIFI_SSID);
    wifiPassword = preferences.getString("pass", DEFAULT_WIFI_PASSWORD);
    String storedMqtt = preferences.getString("mqtt_ip", "");
    uint16_t storedPort = preferences.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
    preferences.end();

    if (storedMqtt.length() > 0) {
        mqttBrokerIP.fromString(storedMqtt);
        mqttBrokerPort = storedPort;
        mqttBrokerFound = true;
        Serial.printf("[NVS] Da tim thay MQTT Broker luu trong Flash: %s:%d\n", storedMqtt.c_str(), storedPort);
    }
}

void saveMQTTConfigToNVS(const String& ipStr, uint16_t port) {
    preferences.begin("syna_net", false);
    preferences.putString("mqtt_ip", ipStr);
    preferences.putUShort("mqtt_port", port);
    preferences.end();
}

void saveWiFiConfigToNVS(const String& ssid, const String& pass) {
    preferences.begin("syna_net", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();
}

void saveMotorConfigToNVS() {
    preferences.begin("syna_motor", false);
    preferences.putUInt("gate_open_ms", gateOpenTimeMs);
    preferences.putUInt("gate_close_ms", gateCloseTimeMs);
    preferences.putUInt("gate_time", gateOpenTimeMs); // Tương thích ngược
    preferences.putUShort("gate_ms_turn", gateMsPerTurn);
    preferences.putUChar("gate_speed", gateSpeed);

    preferences.putUInt("door_open_ms", doorOpenTimeMs);
    preferences.putUInt("door_close_ms", doorCloseTimeMs);
    preferences.putUInt("door_time", doorOpenTimeMs); // Tương thích ngược
    preferences.putUShort("door_ms_turn", doorMsPerTurn);
    preferences.putUChar("door_speed", doorSpeed);
    preferences.end();
    Serial.println("[NVS] Da luu cau hinh rieng biet Thoi gian Dong/Mo vao Flash NVS.");
}

void loadMotorConfigFromNVS() {
    preferences.begin("syna_motor", true);
    uint32_t legacyGate = preferences.getUInt("gate_time", DEFAULT_GATE_OPEN_MS);
    gateOpenTimeMs   = preferences.getUInt("gate_open_ms", legacyGate);
    gateCloseTimeMs  = preferences.getUInt("gate_close_ms", preferences.getUInt("gate_time", DEFAULT_GATE_CLOSE_MS));
    gateTravelTimeMs = gateOpenTimeMs;
    gateMsPerTurn    = preferences.getUShort("gate_ms_turn", DEFAULT_GATE_MS_PER_TURN);
    gateSpeed        = preferences.getUChar("gate_speed", MOTOR_SPEED_DEFAULT);
    currentGateTargetDuration = gateOpenTimeMs;

    uint32_t legacyDoor = preferences.getUInt("door_time", DEFAULT_DOOR_OPEN_MS);
    doorOpenTimeMs   = preferences.getUInt("door_open_ms", legacyDoor);
    doorCloseTimeMs  = preferences.getUInt("door_close_ms", preferences.getUInt("door_time", DEFAULT_DOOR_CLOSE_MS));
    doorTravelTimeMs = doorOpenTimeMs;
    doorMsPerTurn    = preferences.getUShort("door_ms_turn", DEFAULT_DOOR_MS_PER_TURN);
    doorSpeed        = preferences.getUChar("door_speed", MOTOR_SPEED_DEFAULT);
    currentDoorTargetDuration = doorOpenTimeMs;
    preferences.end();

    Serial.println("[NVS] Khoi phuc cau hinh Motor L298N:");
    Serial.printf(" - Cong truot (Gate) : Mo=%ums, Dong=%ums | %ums/vong | Toc do=%d\n",
                  gateOpenTimeMs, gateCloseTimeMs, gateMsPerTurn, gateSpeed);
    Serial.printf(" - Cua cuon/Rem (Door): Mo/Cuon=%ums, Dong/Ha=%ums | %ums/vong | Toc do=%d\n",
                  doorOpenTimeMs, doorCloseTimeMs, doorMsPerTurn, doorSpeed);
}

// ====================================================================================================
// 11. BLE PROVISIONING (CẤU HÌNH TƯƠNG THÍCH SYNA MOBILE APP)
// ====================================================================================================
void notifyBleStatus(const char* status, const char* key1, const char* val1, const char* key2, const char* val2) {
    if (!bleClientConnected || commCharacteristic == nullptr) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["status"] = status;
    doc["nodeId"] = NODE_ID;
    if (key1 && val1) doc[key1] = val1;
    if (key2 && val2) doc[key2] = val2;

    String jsonStr;
    serializeJson(doc, jsonStr);

    commCharacteristic->setValue(jsonStr.c_str());
    commCharacteristic->notify();
    delay(40);
    Serial.printf("[BLE NOTIFY -> APP]: %s\n", jsonStr.c_str());
}

class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        bleClientConnected = true;
        Serial.println("\n[BLE] APP DI DONG DA KET NOI BLE!");
        notifyBleStatus("ble_connected", "device", DEVICE_HUB_NAME);
    }

    void onDisconnect(BLEServer* server) override {
        bleClientConnected = false;
        Serial.println("\n[BLE] APP NGAT KET NOI. Bat lai che do quang ba (Advertising)...");
        BLEDevice::startAdvertising();
    }
};

class BleCommCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        String payload = characteristic->getValue().c_str();
        payload.trim();

        if (payload.length() == 0) return;

        Serial.printf("\n[BLE NHAN GOI TIN]: %s\n", payload.c_str());

#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(512);
#endif
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            notifyBleStatus("error", "message", "invalid_json");
            return;
        }

        const char* action = doc["action"] | "";

        // 1. Cấu hình Wi-Fi từ App
        if (strcmp(action, "set_wifi") == 0) {
            const char* ssid = doc["ssid"] | "";
            const char* pass = doc["password"] | "";

            if (strlen(ssid) == 0) {
                notifyBleStatus("wifi_failed", "error", "empty_ssid");
                return;
            }

            wifiSSID = String(ssid);
            wifiPassword = String(pass);
            saveWiFiConfigToNVS(wifiSSID, wifiPassword);

            if (doc.containsKey("mqtt_host")) {
                const char* mHost = doc["mqtt_host"] | "";
                int mPort = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
                if (strlen(mHost) > 0) {
                    mqttBrokerIP.fromString(mHost);
                    mqttBrokerPort = mPort;
                    mqttBrokerFound = true;
                    saveMQTTConfigToNVS(String(mHost), mPort);
                }
            }

            notifyBleStatus("connecting_wifi", "ssid", wifiSSID.c_str());
            shouldConnectWiFi = true;
            return;
        }

        // 2. Lấy thông tin Node & Devices
        if (strcmp(action, "get_info") == 0) {
            notifyBleStatus("info", "firmware", FIRMWARE_VERSION, "ip", WiFi.localIP().toString().c_str());
            return;
        }

        // 3. Reset cấu hình
        if (strcmp(action, "reset_wifi") == 0) {
            preferences.begin("syna_net", false);
            preferences.clear();
            preferences.end();
            WiFi.disconnect(true);
            mqttClient.disconnect();
            notifyBleStatus("wifi_reset", "message", "config_cleared");
            return;
        }

        notifyBleStatus("error", "message", "unknown_action");
    }
};

void startBLE() {
    Serial.println("[BLE] Dang khoi tao BLE Server...");
    BLEDevice::init(DEVICE_HUB_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new BleServerCallbacks());

    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    commCharacteristic = service->createCharacteristic(
        BLE_COMM_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    commCharacteristic->setCallbacks(new BleCommCallbacks());
    commCharacteristic->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setName(DEVICE_HUB_NAME);
    advData.setFlags(0x06);

    BLEAdvertisementData scanData;
    scanData.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
    Serial.printf("[BLE] Quang ba BLE thanh cong! Ten: %s | UUID: %s\n", DEVICE_HUB_NAME, BLE_SERVICE_UUID);
}

// ====================================================================================================
// 12. KẾT NỐI WIFI & TỰ ĐỘNG DÒ TÌM MQTT BROKER QUA MDNS
// ====================================================================================================
bool connectWiFi() {
    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[WIFI] DANG KET NOI WIFI: %s ...\n", wifiSSID.c_str());
    Serial.println("==================================================");

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
        if (millis() - start > 15000) {
            Serial.println("\n[WIFI ERROR] Ket noi WiFi timeout (Sai SSID/Mat khau hoac song yeu)!");
            notifyBleStatus("wifi_failed", "error", "timeout");
            return false;
        }
    }

    Serial.println();
    Serial.println("==================================================");
    Serial.println("[WIFI] DA KET NOI WIFI THANH CONG!");
    Serial.printf(" - Dia chi IP ESP32: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf(" - Do manh tin hieu: %d dBm\n", WiFi.RSSI());
    Serial.println("==================================================");

    notifyBleStatus("wifi_connected", "ip", WiFi.localIP().toString().c_str());
    return true;
}

bool discoverMQTTBroker() {
    Serial.println();
    Serial.println("[mDNS] Dang do tim Mosquitto Broker (_syna-mqtt._tcp)...");

    if (!MDNS.begin("syna-led-hub")) {
        Serial.println("[mDNS ERROR] Khong the khoi dong mDNS responder!");
    }

    int count = MDNS.queryService(MQTT_DISCOVERY_SERVICE, MQTT_DISCOVERY_PROTOCOL);

    if (count > 0) {
        mqttBrokerIP = MDNS.address(0);
        mqttBrokerPort = MDNS.port(0);
        mqttBrokerFound = true;

        Serial.println("--------------------------------------------------");
        Serial.printf("[mDNS THÀNH CÔNG] Tim thay %d dich vu MQTT Broker!\n", count);
        Serial.printf(" -> Broker IP   : %s\n", mqttBrokerIP.toString().c_str());
        Serial.printf(" -> Broker Port : %d\n", mqttBrokerPort);
        Serial.println("--------------------------------------------------");

        saveMQTTConfigToNVS(mqttBrokerIP.toString(), mqttBrokerPort);
        notifyBleStatus("mdns_discovered", "broker", mqttBrokerIP.toString().c_str(), "port", String(mqttBrokerPort).c_str());
        return true;
    }

    Serial.println("[mDNS CẢNH BÁO] Khong tim thay MQTT qua mDNS. Dung Fallback IP:");
    mqttBrokerIP.fromString(FALLBACK_MQTT_IP);
    mqttBrokerPort = DEFAULT_MQTT_PORT;
    Serial.printf(" -> Fallback Broker IP: %s:%d\n", FALLBACK_MQTT_IP, DEFAULT_MQTT_PORT);
    return false;
}

// ====================================================================================================
// 13. MQTT CLIENT, PUBLISH VÀ XỬ LÝ LỆNH TỪ BACKEND
// ====================================================================================================

/**
 * Kết nối tới MQTT Broker, đăng ký LWT và Subscribe các Topic điều khiển
 */
void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!mqttBrokerFound && mqttBrokerIP == INADDR_NONE) {
        discoverMQTTBroker();
    }

    mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(2048); // Đảm bảo buffer đủ lớn nhận và gửi gói JSON đầy đủ
    mqttClient.setSocketTimeout(1); // Tránh để PubSubClient block loop() 15s khi broker offline

    Serial.println();
    Serial.printf("[MQTT] Dang ket noi toi Broker: %s:%d ...\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);

    String clientId = "HESTA-LED4CH-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    // Chuẩn bị LWT (Last Will and Testament) topic cho node/thiết bị
    String lwtTopic = "hesta/nodes/" NODE_ID "/status";
    const char* lwtPayload = "OFFLINE";

    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
        connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, lwtTopic.c_str(), 1, true, lwtPayload);
    } else {
        connected = mqttClient.connect(clientId.c_str(), lwtTopic.c_str(), 1, true, lwtPayload);
    }

    if (connected) {
        Serial.println("==================================================");
        Serial.println("[MQTT] KET NOI BROKER THANH CONG!");
        Serial.printf(" - Client ID: %s\n", clientId.c_str());
        Serial.println("==================================================");

        // Báo trạng thái Node Online
        mqttClient.publish(lwtTopic.c_str(), "ONLINE", true);

        // Subscribe các Topic điều khiển:
        // 1. Topic wildcard bắt tất cả các lệnh của từng device: hesta/nodes/{nodeId}/devices/+/command
        String subWildcard = "hesta/nodes/" NODE_ID "/devices/+/command";
        mqttClient.subscribe(subWildcard.c_str(), 1);
        Serial.printf("[MQTT SUB] %s\n", subWildcard.c_str());

        // 2. Topic lệnh chung của Node: hesta/nodes/{nodeId}/command
        String subNode = "hesta/nodes/" NODE_ID "/command";
        mqttClient.subscribe(subNode.c_str(), 1);
        Serial.printf("[MQTT SUB] %s\n", subNode.c_str());

        // Báo trạng thái ONLINE và State hiện tại của 4 đèn lên MQTT
        for (int i = 0; i < NUM_LEDS; i++) {
            publishDeviceStatus(i, "ONLINE");
            publishDeviceState(i);
        }

        // Báo trạng thái ONLINE và State ban đầu của Cổng trượt & Cửa cuốn
        String gateStatusTopic = "hesta/nodes/" NODE_ID "/devices/" GATE_DEVICE_ID "/status";
        mqttClient.publish(gateStatusTopic.c_str(), "ONLINE", true);
        publishGateState();

        String doorStatusTopic = "hesta/nodes/" NODE_ID "/devices/" ROLLING_DOOR_DEVICE_ID "/status";
        mqttClient.publish(doorStatusTopic.c_str(), "ONLINE", true);
        publishDoorState();

        publishAllTelemetries();
        publishNodeDiscovery();
    } else {
        Serial.printf("[MQTT ERROR] Ket noi that bai, ma loi rc=%d. Thu lai sau...\n", mqttClient.state());
    }
}

/**
 * Gửi trạng thái State hiện tại của 1 đèn về Backend
 * Topic: hesta/nodes/{nodeId}/devices/{deviceId}/state
 */
void publishDeviceState(uint8_t index) {
    if (!mqttClient.connected() || index >= NUM_LEDS) return;

    String topic = "hesta/nodes/" NODE_ID "/devices/" + String(leds[index].deviceId) + "/state";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = leds[index].deviceId;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["power"]      = leds[index].power;
    doc["brightness"] = leds[index].brightness;

    // Hỗ trợ cả object state con lồng nhau để tương thích tuyệt đối mọi phiên bản Backend
    JsonObject stateObj = doc["state"].to<JsonObject>();
    stateObj["power"]      = leds[index].power;
    stateObj["brightness"] = leds[index].brightness;

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    mqttClient.publish(topic.c_str(), buffer, true);
    Serial.printf("[MQTT STATE -> %s]: %s\n", leds[index].label, buffer);
}

/**
 * Gửi ACK xác nhận thực thi lệnh kèm commandId
 * Topic: hesta/nodes/{nodeId}/devices/{deviceId}/ack
 */
void publishDeviceAck(uint8_t index, const char* commandId, const char* statusMsg) {
    if (!mqttClient.connected() || index >= NUM_LEDS) return;
    if (commandId == nullptr || strlen(commandId) == 0) return;

    String topic = "hesta/nodes/" NODE_ID "/devices/" + String(leds[index].deviceId) + "/ack";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["commandId"] = commandId;
    doc["deviceId"]  = leds[index].deviceId;
    doc["status"]    = statusMsg;

    JsonObject stateObj = doc["state"].to<JsonObject>();
    stateObj["power"]      = leds[index].power;
    stateObj["brightness"] = leds[index].brightness;

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    mqttClient.publish(topic.c_str(), buffer, false);
    Serial.printf("[MQTT ACK -> %s]: %s\n", leds[index].label, buffer);
}

/**
 * Gửi gói Telemetry của thiết bị
 * Topic: hesta/nodes/{nodeId}/devices/{deviceId}/telemetry
 */
void publishDeviceTelemetry(uint8_t index) {
    if (!mqttClient.connected() || index >= NUM_LEDS) return;

    String topic = "hesta/nodes/" NODE_ID "/devices/" + String(leds[index].deviceId) + "/telemetry";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = leds[index].deviceId;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["power"]      = leds[index].power;
    doc["brightness"] = leds[index].brightness;
    doc["duty"]       = (int)round(leds[index].targetDuty);
    doc["uptime"]     = millis() / 1000;

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    mqttClient.publish(topic.c_str(), buffer, false);
}

/**
 * Gửi trạng thái ONLINE / OFFLINE của thiết bị
 * Topic: hesta/nodes/{nodeId}/devices/{deviceId}/status
 */
void publishDeviceStatus(uint8_t index, const char* status) {
    if (!mqttClient.connected() || index >= NUM_LEDS) return;

    String topic = "hesta/nodes/" NODE_ID "/devices/" + String(leds[index].deviceId) + "/status";
    mqttClient.publish(topic.c_str(), status, true);
}

void publishAllTelemetries() {
    for (int i = 0; i < NUM_LEDS; i++) {
        publishDeviceTelemetry(i);
    }
    publishGateTelemetry();
    publishDoorTelemetry();
}

/**
 * Gửi gói Discovery đăng ký Node và danh sách thiết bị
 * Topic: hesta/nodes/{nodeId}/discovery
 */
void publishNodeDiscovery() {
    if (!mqttClient.connected()) return;

    String topic = "hesta/nodes/" NODE_ID "/discovery";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    doc["nodeId"]          = NODE_ID;
    doc["deviceName"]      = DEVICE_HUB_NAME;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["macAddress"]      = WiFi.macAddress();
    doc["ipAddress"]       = WiFi.localIP().toString();
    doc["status"]          = "ONLINE";

    JsonArray devList = doc["devices"].to<JsonArray>();
    for (int i = 0; i < NUM_LEDS; i++) {
        JsonObject d = devList.add<JsonObject>();
        d["deviceId"]   = leds[i].deviceId;
        d["name"]       = leds[i].label;
        d["type"]       = DEVICE_TYPE_LIGHT;
        d["gpio"]       = leds[i].pin;
        d["power"]      = leds[i].power;
        d["brightness"] = leds[i].brightness;
    }

    JsonObject dGate = devList.add<JsonObject>();
    dGate["deviceId"] = GATE_DEVICE_ID;
    dGate["name"]     = "Cổng trượt";
    dGate["type"]     = DEVICE_TYPE_GATE;
    dGate["state"]    = stateToString(gateState);

    JsonObject dDoor = devList.add<JsonObject>();
    dDoor["deviceId"] = ROLLING_DOOR_DEVICE_ID;
    dDoor["name"]     = "Cửa cuốn";
    dDoor["type"]     = DEVICE_TYPE_ROLLING_DOOR;
    dDoor["state"]    = stateToString(doorState);

    char buffer[1024];
    serializeJson(doc, buffer, sizeof(buffer));
    mqttClient.publish(topic.c_str(), buffer, true);
    Serial.printf("[MQTT DISCOVERY]: %s\n", buffer);
}

/**
 * Callback nhận thông điệp MQTT từ Broker
 */
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.println();
    Serial.println("==================================================");
    Serial.printf("[MQTT NHẬN TIN] Topic: %s\n", topic);
    Serial.printf("[MQTT NHẬN TIN] Payload: %s\n", message);
    Serial.println("==================================================");

    String topicStr = String(topic);
    String targetDeviceId = "";

    // Phân tích Device ID từ Topic: hesta/nodes/{nodeId}/devices/{deviceId}/command
    //                                0     1        2        3       4        5
    int devIdx = topicStr.indexOf("/devices/");
    if (devIdx >= 0) {
        int startPos = devIdx + 9;
        int endPos = topicStr.indexOf("/command", startPos);
        if (endPos > startPos) {
            targetDeviceId = topicStr.substring(startPos, endPos);
        }
    }

    processCommandPayload(targetDeviceId, message);
}

/**
 * Xử lý lệnh điều khiển đèn (Power, Brightness, Toggle, Step)
 */
void processCommandPayload(const String& topicDeviceId, const char* payloadStr) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    DeserializationError error = deserializeJson(doc, payloadStr);
    if (error) {
        Serial.printf("[JSON ERROR] Khong the parse JSON: %s\n", error.c_str());
        return;
    }

    // 1. Trích xuất Device ID mục tiêu
    String deviceId = topicDeviceId;
    if (deviceId.length() == 0) {
        if (doc.containsKey("deviceId")) {
            deviceId = (const char*)doc["deviceId"];
        } else if (doc.containsKey("target")) {
            deviceId = (const char*)doc["target"];
        }
    }

    // 2. Trích xuất Action và Command ID
    const char* commandId = doc["commandId"] | "";
    String action = doc["action"] | "";
    action.toUpperCase();

    // 3. Trích xuất tham số từ root hoặc nested object "parameters"
    JsonObject params;
    if (doc["parameters"].is<JsonObject>()) {
        params = doc["parameters"].as<JsonObject>();
    }

    bool hasPowerParam = false;
    bool powerValue = false;
    if (doc.containsKey("power")) {
        hasPowerParam = true;
        if (doc["power"].is<bool>()) {
            powerValue = doc["power"].as<bool>();
        } else {
            String pStr = doc["power"].as<String>();
            pStr.toUpperCase();
            powerValue = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
        }
    } else if (!params.isNull() && params.containsKey("power")) {
        hasPowerParam = true;
        if (params["power"].is<bool>()) {
            powerValue = params["power"].as<bool>();
        } else {
            String pStr = params["power"].as<String>();
            pStr.toUpperCase();
            powerValue = (pStr == "ON" || pStr == "TRUE" || pStr == "1");
        }
    }

    // Hàm trợ giúp parse độ sáng từ JsonVariant (hỗ trợ int, float 0..1 hoặc 0..100, string "80%")
    auto parseBrightnessNumber = [](JsonVariant v) -> int {
        if (v.isNull()) return -1;
        if (v.is<float>() || v.is<double>()) {
            float f = v.as<float>();
            if (f > 0.0f && f <= 1.0f) {
                return (int)round(f * 100.0f);
            }
            return (int)round(f);
        }
        if (v.is<int>()) {
            return v.as<int>();
        }
        if (v.is<const char*>() || v.is<String>()) {
            String s = v.as<String>();
            s.trim();
            if (s.endsWith("%")) {
                s = s.substring(0, s.length() - 1);
            }
            float f = s.toFloat();
            if (f > 0.0f && f <= 1.0f && s.indexOf('.') >= 0) {
                return (int)round(f * 100.0f);
            }
            return (int)round(f);
        }
        return -1;
    };

    bool hasBrightnessParam = false;
    int brightnessValue = -1;

    // Quét các khóa độ sáng phổ biến: brightness, level, value, val, dim, percent
    const char* briKeys[] = {"brightness", "level", "value", "val", "dim", "percent"};
    for (const char* k : briKeys) {
        if (doc.containsKey(k)) {
            int parsed = parseBrightnessNumber(doc[k]);
            if (parsed >= 0) {
                hasBrightnessParam = true;
                brightnessValue = constrain(parsed, 0, 100);
                break;
            }
        }
        if (!params.isNull() && params.containsKey(k)) {
            int parsed = parseBrightnessNumber(params[k]);
            if (parsed >= 0) {
                hasBrightnessParam = true;
                brightnessValue = constrain(parsed, 0, 100);
                break;
            }
        }
    }

    // Quét lượng bước tăng/giảm (Backend Hesta gửi "amount": 20, hoặc "step", "delta")
    bool hasStepAmount = false;
    int stepAmount = 10;
    const char* stepKeys[] = {"amount", "step", "delta", "stepAmount"};
    for (const char* sk : stepKeys) {
        if (doc.containsKey(sk)) {
            int sVal = doc[sk].as<int>();
            if (sVal != 0) {
                hasStepAmount = true;
                stepAmount = abs(sVal);
                break;
            }
        }
        if (!params.isNull() && params.containsKey(sk)) {
            int sVal = params[sk].as<int>();
            if (sVal != 0) {
                hasStepAmount = true;
                stepAmount = abs(sVal);
                break;
            }
        }
    }
    if (!hasStepAmount && hasBrightnessParam && brightnessValue > 0) {
        stepAmount = brightnessValue;
    }

    // 3b. Trích xuất tham số thời gian Mở / Đóng / Chung (duration), vòng quay (turns), tốc độ (speed) cho Motor
    uint32_t cmdDurationMs = 0;
    uint32_t cmdOpenDurationMs = 0;
    uint32_t cmdCloseDurationMs = 0;
    float cmdTurns = 0.0f;
    float cmdOpenTurns = 0.0f;
    float cmdCloseTurns = 0.0f;
    int cmdSpeed = -1;
    uint16_t cmdMsPerTurn = 0;

    auto parseDurationParam = [](JsonVariant v) -> uint32_t {
        if (v.isNull()) return 0;
        if (v.is<float>() || v.is<double>()) {
            float f = v.as<float>();
            // Nếu gửi dạng số giây (vd 2.0 hoặc 1.9)
            if (f > 0.0f && f <= 300.0f) return (uint32_t)(f * 1000.0f);
            return (uint32_t)f;
        }
        if (v.is<uint32_t>() || v.is<int>()) {
            uint32_t val = v.as<uint32_t>();
            // Nếu nhỏ hơn hoặc bằng 120, khả năng là số giây
            if (val > 0 && val <= 120 && !v.as<String>().endsWith("ms")) return val * 1000;
            return val;
        }
        if (v.is<const char*>() || v.is<String>()) {
            String s = v.as<String>();
            s.trim();
            s.toLowerCase();
            if (s.endsWith("ms")) {
                return (uint32_t)s.substring(0, s.length() - 2).toInt();
            }
            if (s.endsWith("s") || s.endsWith("giay") || s.endsWith("sec")) {
                return (uint32_t)(s.toFloat() * 1000.0f);
            }
            uint32_t val = (uint32_t)s.toInt();
            if (val > 0 && val <= 120) return val * 1000;
            return val;
        }
        return 0;
    };

    // Kiểm tra thời gian Mở riêng biệt
    const char* openTimeKeys[] = {"openDuration", "openDurationMs", "openTime", "openTimeMs", "openSeconds", "openSec", "open_duration", "open_time", "thoi_gian_mo"};
    for (const char* k : openTimeKeys) {
        if (doc.containsKey(k)) { cmdOpenDurationMs = parseDurationParam(doc[k]); if (cmdOpenDurationMs > 0) break; }
        if (!params.isNull() && params.containsKey(k)) { cmdOpenDurationMs = parseDurationParam(params[k]); if (cmdOpenDurationMs > 0) break; }
    }

    // Kiểm tra thời gian Đóng riêng biệt
    const char* closeTimeKeys[] = {"closeDuration", "closeDurationMs", "closeTime", "closeTimeMs", "closeSeconds", "closeSec", "close_duration", "close_time", "thoi_gian_dong"};
    for (const char* k : closeTimeKeys) {
        if (doc.containsKey(k)) { cmdCloseDurationMs = parseDurationParam(doc[k]); if (cmdCloseDurationMs > 0) break; }
        if (!params.isNull() && params.containsKey(k)) { cmdCloseDurationMs = parseDurationParam(params[k]); if (cmdCloseDurationMs > 0) break; }
    }

    // Kiểm tra thời gian chung (nếu không tách riêng)
    const char* genTimeKeys[] = {"duration", "durationMs", "time", "timeMs", "seconds", "sec", "thoi_gian"};
    for (const char* k : genTimeKeys) {
        if (doc.containsKey(k)) { cmdDurationMs = parseDurationParam(doc[k]); if (cmdDurationMs > 0) break; }
        if (!params.isNull() && params.containsKey(k)) { cmdDurationMs = parseDurationParam(params[k]); if (cmdDurationMs > 0) break; }
    }

    // Kiểm tra số vòng quay Mở / Đóng
    if (doc.containsKey("openTurns")) cmdOpenTurns = doc["openTurns"].as<float>();
    else if (doc.containsKey("open_turns")) cmdOpenTurns = doc["open_turns"].as<float>();
    else if (doc.containsKey("openVong")) cmdOpenTurns = doc["openVong"].as<float>();
    if (cmdOpenTurns == 0.0f && !params.isNull()) {
        if (params.containsKey("openTurns")) cmdOpenTurns = params["openTurns"].as<float>();
        else if (params.containsKey("open_turns")) cmdOpenTurns = params["open_turns"].as<float>();
        else if (params.containsKey("openVong")) cmdOpenTurns = params["openVong"].as<float>();
    }

    if (doc.containsKey("closeTurns")) cmdCloseTurns = doc["closeTurns"].as<float>();
    else if (doc.containsKey("close_turns")) cmdCloseTurns = doc["close_turns"].as<float>();
    else if (doc.containsKey("closeVong")) cmdCloseTurns = doc["closeVong"].as<float>();
    if (cmdCloseTurns == 0.0f && !params.isNull()) {
        if (params.containsKey("closeTurns")) cmdCloseTurns = params["closeTurns"].as<float>();
        else if (params.containsKey("close_turns")) cmdCloseTurns = params["close_turns"].as<float>();
        else if (params.containsKey("closeVong")) cmdCloseTurns = params["closeVong"].as<float>();
    }

    if (doc.containsKey("turns")) cmdTurns = doc["turns"].as<float>();
    else if (doc.containsKey("vong")) cmdTurns = doc["vong"].as<float>();
    else if (doc.containsKey("rotations")) cmdTurns = doc["rotations"].as<float>();
    if (cmdTurns == 0.0f && !params.isNull()) {
        if (params.containsKey("turns")) cmdTurns = params["turns"].as<float>();
        else if (params.containsKey("vong")) cmdTurns = params["vong"].as<float>();
        else if (params.containsKey("rotations")) cmdTurns = params["rotations"].as<float>();
    }

    // Kiểm tra tốc độ PWM
    if (doc.containsKey("speed")) cmdSpeed = doc["speed"].as<int>();
    else if (doc.containsKey("pwm")) cmdSpeed = doc["pwm"].as<int>();
    if (cmdSpeed < 0 && !params.isNull()) {
        if (params.containsKey("speed")) cmdSpeed = params["speed"].as<int>();
        else if (params.containsKey("pwm")) cmdSpeed = params["pwm"].as<int>();
    }

    if (doc.containsKey("msPerTurn")) cmdMsPerTurn = doc["msPerTurn"].as<uint16_t>();
    else if (doc.containsKey("ms_per_turn")) cmdMsPerTurn = doc["ms_per_turn"].as<uint16_t>();
    if (cmdMsPerTurn == 0 && !params.isNull()) {
        if (params.containsKey("msPerTurn")) cmdMsPerTurn = params["msPerTurn"].as<uint16_t>();
        else if (params.containsKey("ms_per_turn")) cmdMsPerTurn = params["ms_per_turn"].as<uint16_t>();
    }

    // 4. Kiểm tra xem lệnh là Cổng trượt hay Cửa cuốn / Rèm
    if (deviceId.equalsIgnoreCase(GATE_DEVICE_ID) || deviceId.equalsIgnoreCase("gate") || deviceId.equalsIgnoreCase("cong") || deviceId.equalsIgnoreCase("cong_truot") || action.indexOf("GATE") >= 0) {
        if (cmdSpeed >= 0) {
            gateSpeed = constrain(cmdSpeed, 0, 255);
            saveMotorConfigToNVS();
            Serial.printf("[GATE] Da cap nhat toc do PWM: %d\n", gateSpeed);
        }

        if (action == "OPEN" || action == "GATE_OPEN" || action == "UNLOCK" || action == "MO") {
            uint32_t d = (cmdOpenDurationMs > 0) ? cmdOpenDurationMs : cmdDurationMs;
            if (d == 0 && (cmdOpenTurns > 0 || cmdTurns > 0)) {
                float t = (cmdOpenTurns > 0) ? cmdOpenTurns : cmdTurns;
                d = (uint32_t)(t * gateMsPerTurn);
            }
            gateOpen(d, commandId);
        } else if (action == "CLOSE" || action == "GATE_CLOSE" || action == "LOCK" || action == "DONG") {
            uint32_t d = (cmdCloseDurationMs > 0) ? cmdCloseDurationMs : cmdDurationMs;
            if (d == 0 && (cmdCloseTurns > 0 || cmdTurns > 0)) {
                float t = (cmdCloseTurns > 0) ? cmdCloseTurns : cmdTurns;
                d = (uint32_t)(t * gateMsPerTurn);
            }
            gateClose(d, commandId);
        } else if (action == "STOP" || action == "GATE_STOP" || action == "DUNG") {
            gateStop(commandId);
        } else if (action == "SET_OPEN_DURATION" || action == "SET_OPEN_TIME") {
            uint32_t d = (cmdOpenDurationMs > 0) ? cmdOpenDurationMs : cmdDurationMs;
            if (d > 0) {
                setGateOpenDuration(d);
                publishGateState(commandId);
            }
        } else if (action == "SET_CLOSE_DURATION" || action == "SET_CLOSE_TIME") {
            uint32_t d = (cmdCloseDurationMs > 0) ? cmdCloseDurationMs : cmdDurationMs;
            if (d > 0) {
                setGateCloseDuration(d);
                publishGateState(commandId);
            }
        } else if (action == "SET_DURATION" || action == "SET_TIME" || action == "CONFIG_DURATION") {
            if (cmdOpenDurationMs > 0) setGateOpenDuration(cmdOpenDurationMs);
            if (cmdCloseDurationMs > 0) setGateCloseDuration(cmdCloseDurationMs);
            if (cmdOpenDurationMs == 0 && cmdCloseDurationMs == 0 && cmdDurationMs > 0) {
                setGateDuration(cmdDurationMs);
            }
            publishGateState(commandId);
        } else if (action == "SET_TURNS" || action == "SET_VONG") {
            if (cmdOpenTurns > 0 || cmdCloseTurns > 0) {
                setGateTurns((cmdOpenTurns > 0) ? cmdOpenTurns : cmdTurns,
                             (cmdCloseTurns > 0) ? cmdCloseTurns : cmdTurns,
                             cmdMsPerTurn);
            } else if (cmdTurns > 0) {
                setGateTurns(cmdTurns, cmdTurns, cmdMsPerTurn);
            }
            publishGateState(commandId);
        } else if (action == "SET_SPEED") {
            if (cmdSpeed >= 0) {
                gateSpeed = constrain(cmdSpeed, 0, 255);
                saveMotorConfigToNVS();
                publishGateState(commandId);
            }
        } else {
            Serial.printf("[GATE] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    if (deviceId.equalsIgnoreCase(ROLLING_DOOR_DEVICE_ID) || deviceId.equalsIgnoreCase("rolling_door") || deviceId.equalsIgnoreCase("cua_cuon") || deviceId.equalsIgnoreCase("rem") || deviceId.equalsIgnoreCase("curtain") || action.indexOf("ROLLING") >= 0 || action.indexOf("CUA_CUON") >= 0 || action.indexOf("REM") >= 0) {
        if (cmdSpeed >= 0) {
            doorSpeed = constrain(cmdSpeed, 0, 255);
            saveMotorConfigToNVS();
            Serial.printf("[DOOR] Da cap nhat toc do PWM: %d\n", doorSpeed);
        }

        if (action == "OPEN" || action == "DOOR_OPEN" || action == "UP" || action == "UNLOCK" || action == "MO" || action == "CUON_LEN" || action == "MO_REM") {
            uint32_t d = (cmdOpenDurationMs > 0) ? cmdOpenDurationMs : cmdDurationMs;
            if (d == 0 && (cmdOpenTurns > 0 || cmdTurns > 0)) {
                float t = (cmdOpenTurns > 0) ? cmdOpenTurns : cmdTurns;
                d = (uint32_t)(t * doorMsPerTurn);
            }
            doorOpen(d, commandId);
        } else if (action == "CLOSE" || action == "DOOR_CLOSE" || action == "DOWN" || action == "LOCK" || action == "DONG" || action == "HA_XUONG" || action == "DONG_REM") {
            uint32_t d = (cmdCloseDurationMs > 0) ? cmdCloseDurationMs : cmdDurationMs;
            if (d == 0 && (cmdCloseTurns > 0 || cmdTurns > 0)) {
                float t = (cmdCloseTurns > 0) ? cmdCloseTurns : cmdTurns;
                d = (uint32_t)(t * doorMsPerTurn);
            }
            doorClose(d, commandId);
        } else if (action == "STOP" || action == "DOOR_STOP" || action == "DUNG") {
            doorStop(commandId);
        } else if (action == "SET_OPEN_DURATION" || action == "SET_OPEN_TIME") {
            uint32_t d = (cmdOpenDurationMs > 0) ? cmdOpenDurationMs : cmdDurationMs;
            if (d > 0) {
                setDoorOpenDuration(d);
                publishDoorState(commandId);
            }
        } else if (action == "SET_CLOSE_DURATION" || action == "SET_CLOSE_TIME") {
            uint32_t d = (cmdCloseDurationMs > 0) ? cmdCloseDurationMs : cmdDurationMs;
            if (d > 0) {
                setDoorCloseDuration(d);
                publishDoorState(commandId);
            }
        } else if (action == "SET_DURATION" || action == "SET_TIME" || action == "CONFIG_DURATION") {
            if (cmdOpenDurationMs > 0) setDoorOpenDuration(cmdOpenDurationMs);
            if (cmdCloseDurationMs > 0) setDoorCloseDuration(cmdCloseDurationMs);
            if (cmdOpenDurationMs == 0 && cmdCloseDurationMs == 0 && cmdDurationMs > 0) {
                setDoorDuration(cmdDurationMs);
            }
            publishDoorState(commandId);
        } else if (action == "SET_TURNS" || action == "SET_VONG") {
            if (cmdOpenTurns > 0 || cmdCloseTurns > 0) {
                setDoorTurns((cmdOpenTurns > 0) ? cmdOpenTurns : cmdTurns,
                             (cmdCloseTurns > 0) ? cmdCloseTurns : cmdTurns,
                             cmdMsPerTurn);
            } else if (cmdTurns > 0) {
                setDoorTurns(cmdTurns, cmdTurns, cmdMsPerTurn);
            }
            publishDoorState(commandId);
        } else if (action == "SET_SPEED") {
            if (cmdSpeed >= 0) {
                doorSpeed = constrain(cmdSpeed, 0, 255);
                saveMotorConfigToNVS();
                publishDoorState(commandId);
            }
        } else {
            Serial.printf("[ROLLING DOOR] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    // 5. Kiểm tra xem lệnh áp dụng cho TẤT CẢ các đèn hay 1 đèn cụ thể
    bool isAllLeds = (deviceId.equalsIgnoreCase("all") || deviceId.equalsIgnoreCase("all_leds") || deviceId.length() == 0);

    if (isAllLeds) {
        // Thực thi cho toàn bộ các kênh đèn
        if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT") {
            setAllPower(true, commandId);
            if (hasBrightnessParam && brightnessValue > 0) {
                setAllBrightness(brightnessValue, commandId);
            }
        } else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT") {
            setAllPower(false, commandId);
        } else if (action == "TOGGLE") {
            bool anyOn = false;
            for (int i = 0; i < NUM_LEDS; i++) {
                if (leds[i].power) { anyOn = true; break; }
            }
            setAllPower(!anyOn, commandId);
        } else if (action == "SET_BRIGHTNESS" || action == "BRIGHTNESS" || action == "DIM") {
            if (hasBrightnessParam) {
                setAllBrightness(brightnessValue, commandId);
            }
        } else if (action == "INCREASE_BRIGHTNESS" || action == "BRI_UP" || action == "BRIGHTNESS_UP" || 
                   action == "DIM_UP" || action == "TANG" || action == "STEP_UP") {
            stepAllBrightness(stepAmount, commandId);
        } else if (action == "DECREASE_BRIGHTNESS" || action == "BRI_DOWN" || action == "BRIGHTNESS_DOWN" || 
                   action == "DIM_DOWN" || action == "GIAM" || action == "STEP_DOWN") {
            stepAllBrightness(-stepAmount, commandId);
        } else if (action == "SET_POWER") {
            if (hasPowerParam) {
                setAllPower(powerValue, commandId);
            }
            if (hasBrightnessParam && brightnessValue > 0) {
                setAllBrightness(brightnessValue, commandId);
            }
        } else if (hasBrightnessParam) {
            setAllBrightness(brightnessValue, commandId);
        } else if (hasPowerParam) {
            setAllPower(powerValue, commandId);
        } else {
            Serial.printf("[WARN] Action khong ro cho all: %s\n", action.c_str());
        }
        return;
    }

    // 5. Xác định chỉ mục đèn cụ thể (index 0..3)
    int ledIdx = findLedIndexByDeviceId(deviceId);

    if (ledIdx < 0 || ledIdx >= NUM_LEDS) {
        Serial.printf("[ERROR] Khong tim thay LED voi Device ID / Target: [%s]\n", deviceId.c_str());
        return;
    }

    // 6. Thực thi các Action cụ thể
    if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT" || action == "MO") {
        setLedPower(ledIdx, true, commandId);
        if (hasBrightnessParam && brightnessValue > 0) {
            setLedBrightness(ledIdx, brightnessValue, commandId);
        }
    } 
    else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT" || action == "DONG") {
        setLedPower(ledIdx, false, commandId);
    } 
    else if (action == "TOGGLE") {
        toggleLed(ledIdx, commandId);
    } 
    else if (action == "SET_POWER") {
        if (hasPowerParam) {
            setLedPower(ledIdx, powerValue, commandId);
        } else {
            setLedPower(ledIdx, true, commandId);
        }
        if (hasBrightnessParam && brightnessValue > 0) {
            setLedBrightness(ledIdx, brightnessValue, commandId);
        }
    } 
    else if (action == "SET_BRIGHTNESS" || action == "BRIGHTNESS" || action == "DIM") {
        if (hasBrightnessParam) {
            setLedBrightness(ledIdx, brightnessValue, commandId);
        } else {
            Serial.println("[ERROR] SET_BRIGHTNESS thieu gia tri brightness!");
        }
    } 
    else if (action == "INCREASE_BRIGHTNESS" || action == "BRI_UP" || action == "BRIGHTNESS_UP" || 
             action == "DIM_UP" || action == "BRIGHTNESS_PLUS" || action == "TANG" || action == "TANG_DO_SANG" || action == "STEP_UP") {
        stepBrightness(ledIdx, stepAmount, commandId);
    } 
    else if (action == "DECREASE_BRIGHTNESS" || action == "BRI_DOWN" || action == "BRIGHTNESS_DOWN" || 
             action == "DIM_DOWN" || action == "BRIGHTNESS_MINUS" || action == "GIAM" || action == "GIAM_DO_SANG" || action == "STEP_DOWN") {
        stepBrightness(ledIdx, -stepAmount, commandId);
    } 
    else if (action == "GET_STATE" || action == "STATUS") {
        publishDeviceState(ledIdx);
        publishDeviceTelemetry(ledIdx);
    } 
    // Fallback: nếu không có action tường minh nhưng có trường brightness hoặc power
    else if (hasBrightnessParam) {
        setLedBrightness(ledIdx, brightnessValue, commandId);
    } 
    else if (hasPowerParam) {
        setLedPower(ledIdx, powerValue, commandId);
    } 
    else {
        Serial.printf("[WARN] Action chua ho tro: [%s]\n", action.c_str());
    }
}

// ====================================================================================================
// 14. GIAO DIỆN LỆNH QUA SERIAL MONITOR (115200 BAUD)
// ====================================================================================================
void printHelp() {
    Serial.println("\n====================== HƯỚNG DẪN LỆNH SERIAL ======================");
    Serial.println("  1. Chuyển đổi chế độ hoạt động:");
    Serial.println("     - 'mode offline' / 'offline' : Chạy offline (không WiFi/MQTT, loop siêu tốc)");
    Serial.println("     - 'mode online' / 'online'   : Chạy online (kết nối WiFi, MQTT, BLE)");
    Serial.println("  2. Cấu hình kiểm thử phần cứng MOSFET:");
    Serial.println("     - 'fade on' / 'fade off'     : Bật/Tắt hiệu ứng chuyển độ sáng từ từ");
    Serial.println("     - 'polarity high'            : MOSFET Active-High (Duty 255 = Sáng nhất, mặc định)");
    Serial.println("     - 'polarity low'             : MOSFET Active-Low (Duty 0 = Sáng nhất)");
    Serial.println("  3. Điều khiển từng đèn (1 -> 4, hoặc 'led 1', 'den 1'):");
    Serial.println("     - Bật / Tắt          : '1 on', '1 off', '2 on', '2 off' ...");
    Serial.println("     - Đảo trạng thái     : '1 toggle' (hoặc '1 tog')");
    Serial.println("     - Chỉnh độ sáng      : '1 bri 50', '2 b 80', '3 75%', '4 90'");
    Serial.println("     - Tăng / Giảm độ sáng: '1 +', '1 -' (mặc định 10%)");
    Serial.println("                            '1 + 20', '1 - 20', '1 up 15', '1 down 15'");
    Serial.println("                            '1 tang 20', '1 giam 20'");
    Serial.println("  4. Điều khiển đồng loạt tất cả các đèn:");
    Serial.println("     - 'all on' / 'all off'       : Bật / Tắt toàn bộ 4 đèn");
    Serial.println("     - 'all toggle'               : Đảo trạng thái tất cả đèn");
    Serial.println("     - 'all bri 60' / 'all 60%'   : Đặt độ sáng 60% cho cả 4 đèn");
    Serial.println("     - 'all +' / 'all -'          : Tăng/giảm độ sáng toàn bộ 10%");
    Serial.println("     - 'all + 20' / 'all - 20'    : Tăng/giảm độ sáng toàn bộ theo số %");
    Serial.println("     - Phím tắt nhanh             : '+', '-', '+20', '-20', 'bri 80'");
    Serial.println("  5. Điều khiển Cổng trượt (Tách riêng thời gian Mở và Đóng):");
    Serial.println("     - Mở cổng            : 'gate open' hoặc 'gate open 2000' (hoặc 'gate open 2s')");
    Serial.println("     - Đóng cổng          : 'gate close' hoặc 'gate close 1900' (hoặc 'gate close 1.9s')");
    Serial.println("     - Dừng cổng          : 'gate stop'");
    Serial.println("     - Cài thời gian MỞ   : 'gate open_time 2000' (hoặc 'gate opentime 2s')");
    Serial.println("     - Cài thời gian ĐÓNG : 'gate close_time 1900' (hoặc 'gate closetime 1.9s')");
    Serial.println("     - Cài chung Đóng/Mở  : 'gate time 2000'");
    Serial.println("     - Cài số vòng quay   : 'gate open_turns 4', 'gate close_turns 3.8', 'gate turns 4'");
    Serial.println("     - Cài tốc độ PWM     : 'gate speed 120' (0 - 255)");
    Serial.println("  6. Điều khiển Cửa cuốn / Rèm (Tách riêng thời gian Cuộn lên và Hạ xuống):");
    Serial.println("     - Cuộn lên / Mở rèm  : 'door open' hoặc 'door open 2000' (hoặc 'rem open 2000')");
    Serial.println("     - Hạ xuống / Đóng rèm: 'door close' hoặc 'door close 1900' (hoặc 'rem close 1900')");
    Serial.println("     - Dừng cửa / rèm     : 'door stop' (hoặc 'rem stop')");
    Serial.println("     - Cài thời gian MỞ   : 'door open_time 2000' hoặc 'rem open_time 2000'");
    Serial.println("     - Cài thời gian ĐÓNG : 'door close_time 1900' hoặc 'rem close_time 1900'");
    Serial.println("     - Cài chung Đóng/Mở  : 'door time 2000'");
    Serial.println("     - Cài số vòng quay   : 'door open_turns 5', 'door close_turns 4.8', 'door turns 5'");
    Serial.println("     - Cài tốc độ PWM     : 'door speed 120' (0 - 255)");
    Serial.println("  7. Tiện ích hệ thống:");
    Serial.println("     - 'status' / 'info'  : Xem bảng trạng thái 4 đèn, Cổng, Cửa cuốn/Rèm, Wi-Fi, MQTT");
    Serial.println("     - 'telemetry'        : Gửi ngay Telemetry lên MQTT (khi online)");
    Serial.println("     - 'scan'             : Quét lại mDNS tìm MQTT Broker");
    Serial.println("     - 'restart'          : Khởi động lại ESP32");
    Serial.println("===================================================================\n");
}

void printStatus() {
    Serial.println("\n----------------- BẢNG TRẠNG THÁI HỆ THỐNG -----------------");
    Serial.printf("Che do hoat dong : %s\n", offlineTestMode ? "TEST OFFLINE (SERIAL ONLY - Khong giat lag)" : "ONLINE (WIFI + MQTT + BLE)");
    Serial.printf("Node ID          : %s\n", NODE_ID);
    if (!offlineTestMode) {
        Serial.printf("Wi-Fi SSID       : %s (%s) | IP: %s | RSSI: %d dBm\n", 
                      wifiSSID.c_str(), 
                      (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        Serial.printf("MQTT Broker      : %s:%d (%s)\n", 
                      mqttBrokerIP.toString().c_str(), mqttBrokerPort,
                      mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");
    } else {
        Serial.println("Mang (Wi-Fi/MQTT): [DA TAM TAT DE TEST SERIAL KHONG LAG]");
    }
    Serial.printf("Smooth Fading    : %-5s | MOSFET Polarity: %s\n", 
                  enableSmoothFading ? "ON" : "OFF",
                  mosfetActiveHigh ? "ACTIVE-HIGH" : "ACTIVE-LOW");
    Serial.println("------------------------------------------------------------");
    Serial.println("KÊNH | CHÂN GPIO | TRẠNG THÁI | ĐỘ SÁNG | DUTY PWM | DEVICE ID");
    for (int i = 0; i < NUM_LEDS; i++) {
        Serial.printf(" [%d] | GPIO %-3d  | %-10s | %3d%%    | %3d/255  | %s\n",
                      i + 1, leds[i].pin,
                      leds[i].power ? "BẬT (ON)" : "TẮT (OFF)",
                      leds[i].brightness,
                      (int)round(leds[i].currentDuty),
                      leds[i].deviceId);
    }
    Serial.println("------------------------------------------------------------------------------------------------------------------");
    Serial.println("THIẾT BỊ MOTOR (L298N)  | TRẠNG THÁI | THỜI GIAN MỞ    | THỜI GIAN ĐÓNG  | VÒNG (MỞ/ĐÓNG) | PWM | DEVICE ID");
    Serial.printf(" Cổng trượt (Motor A)   | %-10s | %5ums (%.2fs) | %5ums (%.2fs) | %4.1f / %4.1f v  | %3d | %s\n",
                  stateToString(gateState),
                  gateOpenTimeMs, (float)gateOpenTimeMs / 1000.0f,
                  gateCloseTimeMs, (float)gateCloseTimeMs / 1000.0f,
                  (float)gateOpenTimeMs / (float)gateMsPerTurn,
                  (float)gateCloseTimeMs / (float)gateMsPerTurn,
                  gateSpeed,
                  GATE_DEVICE_ID);
    Serial.printf(" Cửa cuốn / Rèm (Mot B) | %-10s | %5ums (%.2fs) | %5ums (%.2fs) | %4.1f / %4.1f v  | %3d | %s\n",
                  stateToString(doorState),
                  doorOpenTimeMs, (float)doorOpenTimeMs / 1000.0f,
                  doorCloseTimeMs, (float)doorCloseTimeMs / 1000.0f,
                  (float)doorOpenTimeMs / (float)doorMsPerTurn,
                  (float)doorCloseTimeMs / (float)doorMsPerTurn,
                  doorSpeed,
                  ROLLING_DOOR_DEVICE_ID);
    Serial.println("------------------------------------------------------------------------------------------------------------------\n");
}

void handleSerialCommands() {
    if (Serial.available() <= 0) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    String lowerCmd = cmd;
    lowerCmd.toLowerCase();

    auto parseCmdTime = [](String str) -> uint32_t {
        str.trim();
        str.toLowerCase();
        if (str.endsWith("ms")) return (uint32_t)str.substring(0, str.length() - 2).toInt();
        if (str.endsWith("s") || str.endsWith("giay") || str.endsWith("sec")) {
            return (uint32_t)(str.toFloat() * 1000.0f);
        }
        uint32_t v = (uint32_t)str.toInt();
        if (v > 0 && v <= 120) return v * 1000;
        return v;
    };

    // 1. Tiện ích hệ thống & Trợ giúp
    if (lowerCmd == "help" || lowerCmd == "?") {
        printHelp();
        return;
    }

    if (lowerCmd == "status" || lowerCmd == "info") {
        printStatus();
        return;
    }

    if (lowerCmd == "telemetry") {
        if (!offlineTestMode) {
            publishAllTelemetries();
            Serial.println("[MANUAL] Da gui Telemetry cua 4 den, Cong truot va Cua cuon len MQTT.");
        } else {
            Serial.println("[MANUAL] Dang o che do Test Offline, khong the gui telemetry MQTT.");
        }
        return;
    }

    if (lowerCmd == "scan") {
        if (!offlineTestMode) {
            discoverMQTTBroker();
        } else {
            Serial.println("[MANUAL] Dang o che do Test Offline. Go 'mode online' de ket noi Wi-Fi & mDNS.");
        }
        return;
    }

    if (lowerCmd == "restart" || lowerCmd == "reboot") {
        Serial.println("[SYSTEM] Dang khoi dong lai ESP32...");
        delay(500);
        ESP.restart();
        return;
    }

    // 2. Chuyển đổi chế độ hoạt động (Offline / Online)
    if (lowerCmd == "mode offline" || lowerCmd == "offline") {
        offlineTestMode = true;
        Serial.println("\n[MODE] === DA CHUYEN SANG CHE DO TEST OFFLINE (SERIAL ONLY) ===");
        Serial.println("-> Da ngat/dung ket noi Wi-Fi va MQTT. Loop chay sieu toc, phan hoi ngay lap tuc.");
        return;
    }

    if (lowerCmd == "mode online" || lowerCmd == "online") {
        offlineTestMode = false;
        Serial.println("\n[MODE] === DA CHUYEN SANG CHE DO ONLINE ===");
        Serial.println("-> Dang ket noi Wi-Fi, mDNS va MQTT Broker...");
        connectWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            discoverMQTTBroker();
            connectMQTT();
        }
        return;
    }

    // 3. Cấu hình kiểm thử phần cứng (Fade & MOSFET Polarity)
    if (lowerCmd.startsWith("fade")) {
        String sub = lowerCmd.substring(4);
        sub.trim();
        if (sub == "on" || sub == "1" || sub == "true" || sub == "enable") {
            enableSmoothFading = true;
            Serial.println("[CONFIG] Da BAT hieu ung Smooth Fading.");
        } else if (sub == "off" || sub == "0" || sub == "false" || sub == "disable") {
            enableSmoothFading = false;
            for (int i = 0; i < NUM_LEDS; i++) {
                leds[i].currentDuty = leds[i].targetDuty;
                writeHardwarePWM(i, (uint8_t)round(leds[i].currentDuty));
            }
            Serial.println("[CONFIG] Da TAT Smooth Fading (do sang thay doi tuc thi).");
        } else {
            enableSmoothFading = !enableSmoothFading;
            Serial.printf("[CONFIG] Smooth Fading: %s\n", enableSmoothFading ? "ON" : "OFF");
        }
        return;
    }

    if (lowerCmd.startsWith("polarity") || lowerCmd.startsWith("cuc")) {
        int spaceIdx = lowerCmd.indexOf(' ');
        if (spaceIdx > 0) {
            String sub = lowerCmd.substring(spaceIdx + 1);
            sub.trim();
            if (sub == "high" || sub == "1" || sub == "active_high") {
                mosfetActiveHigh = true;
                Serial.println("[CONFIG] Da dat cuc MOSFET: ACTIVE-HIGH (Duty cao = Sang hon).");
            } else if (sub == "low" || sub == "0" || sub == "active_low") {
                mosfetActiveHigh = false;
                Serial.println("[CONFIG] Da dat cuc MOSFET: ACTIVE-LOW (Duty thap = Sang hon).");
            }
            for (int i = 0; i < NUM_LEDS; i++) {
                writeHardwarePWM(i, (uint8_t)round(leds[i].currentDuty));
            }
        } else {
            Serial.printf("[CONFIG] Cuc MOSFET hien tai: %s\n", mosfetActiveHigh ? "ACTIVE-HIGH" : "ACTIVE-LOW");
        }
        return;
    }

    // 4. Lệnh Cổng trượt (Sliding Gate)
    if (lowerCmd.startsWith("gate ") || lowerCmd.startsWith("cong ")) {
        String sub = lowerCmd.substring(lowerCmd.indexOf(' ') + 1);
        sub.trim();

        if (sub.startsWith("open_time ") || sub.startsWith("opentime ") || sub.startsWith("mo_time ") || sub.startsWith("motime ")) {
            int spaceIdx = sub.indexOf(' ');
            uint32_t val = parseCmdTime(sub.substring(spaceIdx + 1));
            if (val > 0) setGateOpenDuration(val);
            publishGateState();
            return;
        }
        if (sub.startsWith("close_time ") || sub.startsWith("closetime ") || sub.startsWith("dong_time ") || sub.startsWith("dongtime ")) {
            int spaceIdx = sub.indexOf(' ');
            uint32_t val = parseCmdTime(sub.substring(spaceIdx + 1));
            if (val > 0) setGateCloseDuration(val);
            publishGateState();
            return;
        }
        if (sub.startsWith("open_turns ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) setGateTurns(val, (float)gateCloseTimeMs / (float)gateMsPerTurn);
            publishGateState();
            return;
        }
        if (sub.startsWith("close_turns ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) setGateTurns((float)gateOpenTimeMs / (float)gateMsPerTurn, val);
            publishGateState();
            return;
        }
        if (sub.startsWith("open") || sub.startsWith("mo")) {
            uint32_t customMs = 0;
            int spaceIdx = sub.indexOf(' ');
            if (spaceIdx > 0) {
                customMs = parseCmdTime(sub.substring(spaceIdx + 1));
            }
            gateOpen(customMs);
            return;
        }
        if (sub.startsWith("close") || sub.startsWith("dong")) {
            uint32_t customMs = 0;
            int spaceIdx = sub.indexOf(' ');
            if (spaceIdx > 0) {
                customMs = parseCmdTime(sub.substring(spaceIdx + 1));
            }
            gateClose(customMs);
            return;
        }
        if (sub == "stop" || sub == "dung") {
            gateStop();
            return;
        }
        if (sub.startsWith("time ") || sub.startsWith("duration ")) {
            uint32_t val = parseCmdTime(sub.substring(sub.indexOf(' ') + 1));
            if (val > 0) {
                setGateDuration(val);
                publishGateState();
            }
            return;
        }
        if (sub.startsWith("turns ") || sub.startsWith("vong ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) {
                setGateTurns(val, val);
                publishGateState();
            }
            return;
        }
        if (sub.startsWith("speed ") || sub.startsWith("pwm ")) {
            int val = sub.substring(sub.indexOf(' ') + 1).toInt();
            gateSpeed = constrain(val, 0, 255);
            saveMotorConfigToNVS();
            Serial.printf("[GATE] Da cap nhat toc do PWM: %d\n", gateSpeed);
            publishGateState();
            return;
        }
    }

    // 5. Lệnh Cửa cuốn / Rèm (Rolling Door / Curtain)
    if (lowerCmd.startsWith("door ") || lowerCmd.startsWith("rem ") || lowerCmd.startsWith("curtain ") || lowerCmd.startsWith("rolling ") || lowerCmd.startsWith("cua_cuon ") || lowerCmd.startsWith("cuacuon ")) {
        String sub = lowerCmd.substring(lowerCmd.indexOf(' ') + 1);
        sub.trim();

        if (sub.startsWith("open_time ") || sub.startsWith("opentime ") || sub.startsWith("mo_time ") || sub.startsWith("motime ") || sub.startsWith("cuon_len_time ")) {
            int spaceIdx = sub.indexOf(' ');
            uint32_t val = parseCmdTime(sub.substring(spaceIdx + 1));
            if (val > 0) setDoorOpenDuration(val);
            publishDoorState();
            return;
        }
        if (sub.startsWith("close_time ") || sub.startsWith("closetime ") || sub.startsWith("dong_time ") || sub.startsWith("dongtime ") || sub.startsWith("ha_xuong_time ")) {
            int spaceIdx = sub.indexOf(' ');
            uint32_t val = parseCmdTime(sub.substring(spaceIdx + 1));
            if (val > 0) setDoorCloseDuration(val);
            publishDoorState();
            return;
        }
        if (sub.startsWith("open_turns ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) setDoorTurns(val, (float)doorCloseTimeMs / (float)doorMsPerTurn);
            publishDoorState();
            return;
        }
        if (sub.startsWith("close_turns ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) setDoorTurns((float)doorOpenTimeMs / (float)doorMsPerTurn, val);
            publishDoorState();
            return;
        }
        if (sub.startsWith("open") || sub.startsWith("mo") || sub.startsWith("up") || sub.startsWith("cuon_len")) {
            uint32_t customMs = 0;
            int spaceIdx = sub.indexOf(' ');
            if (spaceIdx > 0) {
                customMs = parseCmdTime(sub.substring(spaceIdx + 1));
            }
            doorOpen(customMs);
            return;
        }
        if (sub.startsWith("close") || sub.startsWith("dong") || sub.startsWith("down") || sub.startsWith("ha_xuong")) {
            uint32_t customMs = 0;
            int spaceIdx = sub.indexOf(' ');
            if (spaceIdx > 0) {
                customMs = parseCmdTime(sub.substring(spaceIdx + 1));
            }
            doorClose(customMs);
            return;
        }
        if (sub == "stop" || sub == "dung") {
            doorStop();
            return;
        }
        if (sub.startsWith("time ") || sub.startsWith("duration ")) {
            uint32_t val = parseCmdTime(sub.substring(sub.indexOf(' ') + 1));
            if (val > 0) {
                setDoorDuration(val);
                publishDoorState();
            }
            return;
        }
        if (sub.startsWith("turns ") || sub.startsWith("vong ")) {
            float val = sub.substring(sub.indexOf(' ') + 1).toFloat();
            if (val > 0) {
                setDoorTurns(val, val);
                publishDoorState();
            }
            return;
        }
        if (sub.startsWith("speed ") || sub.startsWith("pwm ")) {
            int val = sub.substring(sub.indexOf(' ') + 1).toInt();
            doorSpeed = constrain(val, 0, 255);
            saveMotorConfigToNVS();
            Serial.printf("[DOOR] Da cap nhat toc do PWM: %d\n", doorSpeed);
            publishDoorState();
            return;
        }
    }

    // 6. Phím tắt tăng giảm nhanh toàn bộ đèn: "+", "-", "+20", "-20", "+ 20", "- 20", "tang", "giam", "bri 70"
    if (lowerCmd == "+" || lowerCmd == "up" || lowerCmd == "tang") {
        stepAllBrightness(10);
        return;
    }
    if (lowerCmd == "-" || lowerCmd == "down" || lowerCmd == "giam") {
        stepAllBrightness(-10);
        return;
    }
    if (lowerCmd.startsWith("+")) {
        String numPart = lowerCmd.substring(1);
        numPart.trim();
        int val = numPart.toInt();
        if (val <= 0) val = 10;
        stepAllBrightness(val);
        return;
    }
    if (lowerCmd.startsWith("-")) {
        String numPart = lowerCmd.substring(1);
        numPart.trim();
        int val = numPart.toInt();
        if (val <= 0) val = 10;
        stepAllBrightness(-val);
        return;
    }
    if (lowerCmd.startsWith("bri ") || lowerCmd.startsWith("b ")) {
        int val = lowerCmd.substring(lowerCmd.indexOf(' ') + 1).toInt();
        setAllBrightness(val);
        return;
    }

    // 7. Lệnh cho tất cả đèn: "all ...", "tatca ..."
    if (lowerCmd.startsWith("all ") || lowerCmd.startsWith("tatca ") || lowerCmd.startsWith("all_")) {
        int spaceIdx = lowerCmd.indexOf(' ');
        if (spaceIdx < 0) spaceIdx = lowerCmd.indexOf('_');
        String sub = lowerCmd.substring(spaceIdx + 1);
        sub.trim();

        if (sub == "on" || sub == "bat") {
            setAllPower(true);
        } else if (sub == "off" || sub == "tat") {
            setAllPower(false);
        } else if (sub == "toggle" || sub == "tog" || sub == "dao") {
            bool anyOn = false;
            for (int i = 0; i < NUM_LEDS; i++) {
                if (leds[i].power) { anyOn = true; break; }
            }
            setAllPower(!anyOn);
        } else if (sub == "+" || sub == "up" || sub == "tang") {
            stepAllBrightness(10);
        } else if (sub.startsWith("+") || sub.startsWith("up ") || sub.startsWith("tang ") || sub.startsWith("+ ")) {
            int val = 10;
            if (sub.startsWith("+")) {
                String numPart = sub.substring(1);
                numPart.trim();
                val = numPart.toInt();
            } else {
                val = sub.substring(sub.indexOf(' ') + 1).toInt();
            }
            if (val <= 0) val = 10;
            stepAllBrightness(val);
        } else if (sub == "-" || sub == "down" || sub == "giam") {
            stepAllBrightness(-10);
        } else if (sub.startsWith("-") || sub.startsWith("down ") || sub.startsWith("giam ") || sub.startsWith("- ")) {
            int val = 10;
            if (sub.startsWith("-")) {
                String numPart = sub.substring(1);
                numPart.trim();
                val = numPart.toInt();
            } else {
                val = sub.substring(sub.indexOf(' ') + 1).toInt();
            }
            if (val <= 0) val = 10;
            stepAllBrightness(-val);
        } else if (sub.startsWith("bri ") || sub.startsWith("b ")) {
            int val = sub.substring(sub.indexOf(' ') + 1).toInt();
            setAllBrightness(val);
        } else if (sub.endsWith("%")) {
            int val = sub.substring(0, sub.length() - 1).toInt();
            setAllBrightness(val);
        } else {
            int val = sub.toInt();
            if (val > 0 || sub == "0") {
                setAllBrightness(val);
            } else {
                Serial.println("[ERROR] Cu phap cho tat ca den khong hop le! Go 'help' de xem huong dan.");
            }
        }
        return;
    }

    // 8. Lệnh cho từng đèn (hỗ trợ "1 ...", "led 1 ...", "led1 ...", "den 1 ...", "den1 ...")
    String targetStr = lowerCmd;
    if (targetStr.startsWith("led ") || targetStr.startsWith("den ")) {
        targetStr = targetStr.substring(4);
        targetStr.trim();
    } else if (targetStr.startsWith("led") || targetStr.startsWith("den")) {
        targetStr = targetStr.substring(3);
        targetStr.trim();
    }

    char firstChar = targetStr.charAt(0);
    if (firstChar >= '1' && firstChar <= '4') {
        int idx = (firstChar - '1');
        String sub = targetStr.substring(1);
        sub.trim();

        if (sub == "on" || sub == "bat") {
            setLedPower(idx, true);
        } else if (sub == "off" || sub == "tat") {
            setLedPower(idx, false);
        } else if (sub == "toggle" || sub == "tog" || sub == "dao") {
            toggleLed(idx);
        } else if (sub == "+" || sub == "up" || sub == "tang") {
            stepBrightness(idx, 10);
        } else if (sub.startsWith("+") || sub.startsWith("up ") || sub.startsWith("tang ") || sub.startsWith("+ ")) {
            int val = 10;
            if (sub.startsWith("+")) {
                String numPart = sub.substring(1);
                numPart.trim();
                val = numPart.toInt();
            } else {
                val = sub.substring(sub.indexOf(' ') + 1).toInt();
            }
            if (val <= 0) val = 10;
            stepBrightness(idx, val);
        } else if (sub == "-" || sub == "down" || sub == "giam") {
            stepBrightness(idx, -10);
        } else if (sub.startsWith("-") || sub.startsWith("down ") || sub.startsWith("giam ") || sub.startsWith("- ")) {
            int val = 10;
            if (sub.startsWith("-")) {
                String numPart = sub.substring(1);
                numPart.trim();
                val = numPart.toInt();
            } else {
                val = sub.substring(sub.indexOf(' ') + 1).toInt();
            }
            if (val <= 0) val = 10;
            stepBrightness(idx, -val);
        } else if (sub.startsWith("bri ") || sub.startsWith("b ")) {
            int spaceIdx = sub.indexOf(' ');
            int val = sub.substring(spaceIdx + 1).toInt();
            setLedBrightness(idx, val);
        } else if (sub.endsWith("%")) {
            int val = sub.substring(0, sub.length() - 1).toInt();
            setLedBrightness(idx, val);
        } else {
            // Thử xem sub có phải là số độ sáng trực tiếp không, ví dụ: "1 80"
            int val = sub.toInt();
            if (val > 0 || sub == "0") {
                setLedBrightness(idx, val);
            } else {
                Serial.println("[ERROR] Cu phap khong hop le! Go 'help' de xem huong dan.");
            }
        }
        return;
    }

    Serial.println("[ERROR] Lenh khong xac dinh! Go 'help' de xem danh sach lenh hop le.");
}

// ====================================================================================================
// 15. ARDUINO SETUP & LOOP
// ====================================================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n====================================================================");
    Serial.println("  HESTA / SYNA SMART HOME - ESP32 4CH LED + GATE & ROLLING DOOR");
    Serial.println("  NODE ID : " NODE_ID);
    Serial.println("  FIRMWARE: " FIRMWARE_VERSION);
    Serial.println("====================================================================");

    // 1. Khởi tạo phần cứng PWM và chân MOSFET đèn
    initPWM();

    // 2. Khởi tạo phần cứng Cổng trượt & Cửa cuốn (Motor L298N theo thời gian / vòng quay)
    initMotorHardware();

    // 3. Khôi phục trạng thái từ Flash NVS
    loadAllStatesFromNVS();
    loadMotorConfigFromNVS();
    loadNetworkConfigFromNVS();

    // 4. Kiểm tra chế độ chạy (Offline Test vs Online đầy đủ)
    if (offlineTestMode) {
        Serial.println("\n====================================================================");
        Serial.println("[MODE] DANG CHAY CHE DO: TEST OFFLINE (SERIAL ONLY)");
        Serial.println(" -> Da bo qua ket noi Wi-Fi, MQTT va BLE.");
        Serial.println(" -> Vong loop() chay sieu toc, 0 giay timeout, phan hoi ngay lap tuc.");
        Serial.println(" -> De bat ket noi Online: go lenh 'mode online' tren Serial");
        Serial.println("    hoac sua define OFFLINE_TEST_MODE false trong code.");
        Serial.println("====================================================================");
    } else {
        // Khởi tạo BLE Provisioning (hỗ trợ App SYNA)
        startBLE();

        // Kết nối Wi-Fi
        connectWiFi();

        // Dò tìm mDNS và kết nối MQTT Broker
        if (WiFi.status() == WL_CONNECTED) {
            discoverMQTTBroker();
            connectMQTT();
        }
    }

    printHelp();
    printStatus();
    Serial.println("-> He thong da san sang nhan lenh qua Serial Monitor...\n");
}

void loop() {
    unsigned long now = millis();

    // 1. Cập nhật hiệu ứng Smooth Fading mềm mại cho các đèn
    if (now - lastFadingUpdate >= 15) {
        lastFadingUpdate = now;
        updateSmoothFading();
    }

    // 2. Chỉ quản lý mạng khi KHÔNG ở chế độ Offline Test Mode
    if (!offlineTestMode) {
        // 2.1 Kiểm tra cờ kết nối Wi-Fi khi nhận thông tin từ BLE App
        if (shouldConnectWiFi) {
            shouldConnectWiFi = false;
            if (connectWiFi()) {
                discoverMQTTBroker();
                connectMQTT();
            }
        }

        // 2.2 Tự động kết nối lại Wi-Fi nếu bị rớt mạng
        if (WiFi.status() != WL_CONNECTED) {
            if (now - lastWiFiReconnectAttempt >= 10000) {
                lastWiFiReconnectAttempt = now;
                Serial.println("[WIFI AUTO-RECONNECT] Dang thu ket noi lai Wi-Fi...");
                if (WiFi.reconnect()) {
                    Serial.println("[WIFI AUTO-RECONNECT] Da ket noi lai WiFi!");
                }
            }
        } else {
            // 2.3 Quản lý kết nối MQTT Broker
            if (!mqttClient.connected()) {
                if (now - lastMqttReconnectAttempt >= 5000) {
                    lastMqttReconnectAttempt = now;
                    Serial.println("[MQTT AUTO-RECONNECT] Mat ket noi MQTT, dang thu ket noi lai...");
                    connectMQTT();
                }
            } else {
                mqttClient.loop();

                // 2.4 Gửi Telemetry định kỳ
                if (now - lastTelemetryPublish >= TELEMETRY_INTERVAL_MS) {
                    lastTelemetryPublish = now;
                    publishAllTelemetries();
                }
            }
        }
    }

    // 3. Giám sát thời gian chạy và tự động dừng cho Cổng trượt & Cửa cuốn
    updateMotorTimers();

    // 4. Xử lý các lệnh tương tác từ Serial Monitor
    handleSerialCommands();
}
