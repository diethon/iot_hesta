/*
 * ====================================================================================================
 * DỰ ÁN: HESTA / SYNA SMART HOME - BỘ ĐIỀU KHIỂN CẢM BIẾN + ĐÈN PHÒNG KHÁCH + ĐÈN GARAGE + QUẠT + MÁY BƠM + CỬA CHÍNH
 * THIẾT KẾ DÀNH CHO: ESP32 / ESP32-S3 (TƯƠNG THÍCH HOÀN TOÀN ESP32-S3 N16R8 & ESP32 WROOM/WROVER)
 * ====================================================================================================
 * 
 * 1. THÔNG TIN HỆ THỐNG (ĐỊNH DANH BACKEND HESTA):
 * ----------------------------------------------------------------------------------------------------
 * - NODE ID              : 109441d8-b6bf-4a18-bdf8-9279b7a1548d
 * - DEVICE CỬA CHÍNH     : 70de950a-14b3-4b44-ac67-e2ff4e2c5d17 (Servo SG90 - GPIO 8, Tốc độ 0 = Mở tức thì)
 * - DEVICE ĐÈN PHÒNG     : b016fe3d-36fc-4d3e-a8bd-e287d7138395 (Đèn phòng khách - GPIO 7 MOSFET/PWM)
 * - DEVICE ĐÈN GARAGE    : f23a13d8-1498-499d-aac2-04cee145f020 (Đèn garage - GPIO 10 MOSFET/PWM/Relay)
 * - DEVICE QUẠT          : 6643cf42-bb8e-434e-a440-743afe06c457 (Quạt thông minh - GPIO 19 Relay)
 * - DEVICE MÁY BƠM NƯỚC  : 664017ce-c045-4b0e-9a26-f9f95f996803 (Máy bơm nước / Motor - GPIO 18 Relay)
 * - DEVICE ÁNH SÁNG LDR  : 57c13343-f2cd-45ce-b65d-0bd843ae95d3 (Cảm biến ánh sáng LDR - GPIO 4)
 * - DEVICE CHUYỂN ĐỘNG   : 5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2 (Cảm biến PIR - GPIO 5)
 * - DEVICE NHIỆT ẨM DHT  : 0bb7dc6e-0556-413d-a507-66bc8225b4ba (Cảm biến DHT22 - GPIO 6)
 * - DEVICE CẢM BIẾN LỬA  : a41fd107-29de-4e97-a117-35947e124a03 (Cảm biến lửa Flame Sensor - GPIO 14)
 * - DEVICE CẢM BIẾN KHÓI : 9e745160-b157-440f-a933-79919cb37be3 (Cảm biến khói MH MQ Sensor - GPIO 15)
 * 
 * 2. NGUYÊN LÝ HOẠT ĐỘNG & SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG:
 * ----------------------------------------------------------------------------------------------------
 * - Cửa chính (Main Door): Điều khiển Servo SG90 (GPIO 8, PWM 50Hz, Tốc độ = 0 tức là mở/đóng tức thì).
 * - Đèn phòng khách: PWM 5000Hz qua MOSFET (GPIO 7), độ sáng 0 - 100%, hỗ trợ hiệu ứng Smooth Fading (sáng dần / tối dần khi bật tắt tương tự LED_Normal), độc lập không bị kích hoạt bởi PIR.
 * - Đèn garage: PWM 5000Hz qua MOSFET/Relay (GPIO 10), độ sáng 0 - 100%, tự động bật khi có chuyển động.
 * - Quạt thông minh (Smart Fan): Điều khiển Relay qua GPIO 19, hỗ trợ bật/tắt và chọn cấp tốc độ (1, 2, 3).
 * - Máy bơm nước (Water Pump / Motor): Điều khiển Relay qua GPIO 18, hỗ trợ bật/tắt độc lập hoặc đồng thời.
 * - Cảm biến ánh sáng (LDR): GPIO 4 (Sáng = LOW, Tối = HIGH).
 * - Cảm biến chuyển động (PIR): GPIO 5 (Có chuyển động = HIGH).
 * - Cảm biến nhiệt độ & độ ẩm: DHT22 (GPIO 6).
 * - Cảm biến vân tay AS608: UART1 (AS608 TX -> GPIO 16, AS608 RX -> GPIO 17, V+ -> 3V3).
 *   Vân tay hợp lệ -> MỞ CỬA CHÍNH (70de950a-...) -> tự đóng sau 5s. Đăng ký/xóa qua lệnh Serial 'fp ...'.
 * - Tự động hóa PIR Auto Light:
 *   + Khi trời tối (LDR) & phát hiện chuyển động (PIR) -> TỰ ĐỘNG BẬT ĐÈN GARAGE (f23a13d8-1498-499d-aac2-04cee145f020).
 *   + Khi đèn garage đang sáng mà sau 5 giây không có chuyển động (PIR = LOW liên tục 5s) -> TỰ ĐỘNG TẮT ĐÈN GARAGE.
 *   + Đèn phòng khách KHÔNG bị ảnh hưởng bởi cảm biến chuyển động (vẫn giữ nguyên trạng thái).
 *   + Nếu đèn garage vừa tắt thủ công trong 5 giây -> Bỏ qua cảm biến (tránh bật lại khi vừa tắt đèn đi ra ngoài).
 * 
 * 3. TÍNH NĂNG KẾT NỐI BACKEND (CHUẨN HỆ SINH THÁI SYNA / HESTA):
 * ----------------------------------------------------------------------------------------------------
 * - BLE Provisioning chuẩn SYNA Mobile App (1 Service, 1 Char UUID).
 * - Kết nối Wi-Fi & MQTT tự phục hồi khi rớt mạng, dò mDNS Mosquitto Broker.
 * - Hỗ trợ LWT (Last Will & Testament), Discovery Payload đăng ký Node & Toàn bộ 10 thiết bị.
 * - Phản hồi ACK, State, Telemetry đầy đủ về Backend Hesta.
 * - Chế độ Test Offline (Serial Test Mode) giúp test thiết bị tức thì không lo timeout socket.
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
#include <DHT.h>
#include <Adafruit_Fingerprint.h>   // Thư viện "Adafruit Fingerprint Sensor Library" (hỗ trợ AS608 / R307 / R503)

// ====================================================================================================
// 1. CẤU HÌNH NODE & DEVICE IDS (HESTA BACKEND / DATABASE)
// ====================================================================================================
#define NODE_ID "109441d8-b6bf-4a18-bdf8-9279b7a1548d"

// Thiết bị Cụm cảm biến môi trường & An toàn PCCC
#define LDR_DEVICE_ID           "57c13343-f2cd-45ce-b65d-0bd843ae95d3"
#define PIR_DEVICE_ID           "5e854e0e-612b-4aa5-95c8-e9fd44f2b8f2"
#define DHT_DEVICE_ID           "0bb7dc6e-0556-413d-a507-66bc8225b4ba"
#define FLAME_DEVICE_ID         "a41fd107-29de-4e97-a117-35947e124a03"
#define SMOKE_DEVICE_ID         "9e745160-b157-440f-a933-79919cb37be3"

// Thiết bị 1: Đèn phòng khách (Living Room Light)
#define LIVING_LIGHT_DEVICE_ID  "b016fe3d-36fc-4d3e-a8bd-e287d7138395"
#define LIGHT_DEVICE_ID         LIVING_LIGHT_DEVICE_ID // Alias tương thích

// Thiết bị 2: Đèn garage (Garage Light - Tự động bật khi phát hiện chuyển động)
#define GARAGE_LIGHT_DEVICE_ID  "f23a13d8-1498-499d-aac2-04cee145f020"

// Thiết bị 3: Quạt thông minh (Smart Fan)
#define FAN_DEVICE_ID           "6643cf42-bb8e-434e-a440-743afe06c457"

// Thiết bị 4: Máy bơm nước (Water Pump / Motor)
#define PUMP_DEVICE_ID          "664017ce-c045-4b0e-9a26-f9f95f996803"

// Thiết bị 5: Cửa chính (Main Door - Servo SG90)
#define MAIN_DOOR_DEVICE_ID     "70de950a-14b3-4b44-ac67-e2ff4e2c5d17"

#define DEVICE_TYPE_LIGHT        "LIGHT"
#define DEVICE_TYPE_DOOR         "DOOR"
#define DEVICE_TYPE_FAN          "FAN"
#define DEVICE_TYPE_PUMP         "PUMP"
#define DEVICE_TYPE_LDR          "LIGHT_SENSOR"
#define DEVICE_TYPE_PIR          "MOTION_SENSOR"
#define DEVICE_TYPE_DHT          "DHT"
#define DEVICE_TYPE_FLAME        "FLAME_SENSOR"
#define DEVICE_TYPE_SMOKE        "SMOKE_SENSOR"

// ====================================================================================================
// 2. CHẾ ĐỘ TEST OFFLINE (SERIAL TEST MODE) & CẤU HÌNH PHẦN CỨNG
// ====================================================================================================
// Đặt true : BỎ QUA kết nối Wi-Fi, MQTT, BLE. Dành riêng để test cảm biến, 2 đèn, quạt, bơm, Servo qua Serial.
//            Vòng lặp loop() chạy mượt mà tức thì, không bị đứng/lag bởi socket timeout 15s.
// Đặt false: Chạy đầy đủ hệ thống thật (BLE Provisioning -> Wi-Fi -> mDNS -> MQTT Broker).
#define OFFLINE_TEST_MODE false
bool offlineTestMode = OFFLINE_TEST_MODE;

// --- 2.1 CẢM BIẾN MÔI TRƯỜNG & AN TOÀN PCCC ---
#define PIN_LDR                 4    // Cảm biến ánh sáng (Digital / Analog)
#define LDR_DARK_LEVEL          HIGH // Mức logic khi trời tối (Module LM393: Sáng = LOW, Tối = HIGH)
#define PIN_PIR                 5    // Cảm biến chuyển động PIR
#define PIN_DHT                 6    // Cảm biến nhiệt ẩm DHT22
#define DHT_TYPE                DHT22

// Cảm biến lửa (Flame Sensor): DO nối GPIO 14
#define PIN_FLAME               14   // Chân tín hiệu DO của Cảm biến lửa
#define FLAME_DETECTED_LEVEL    LOW // Mức logic khi có lửa (Module LM393: Bình thường = HIGH, Có lửa = LOW)

// Cảm biến khói (MH MQ Sensor): DO nối GPIO 15
#define PIN_SMOKE               15   // Chân tín hiệu DO của Cảm biến khói MH MQ
#define SMOKE_DETECTED_LEVEL    LOW  // Mức logic khi có khói/gas (Module MQ: Bình thường = HIGH, Vượt ngưỡng = LOW)

// --- 2.2 ĐÈN PHÒNG KHÁCH (LIVING ROOM LIGHT - MOSFET / PWM) ---
#define PIN_LIVING_LIGHT        7    // Chân điều khiển đèn phòng khách (GPIO 7)
#define PWM_LIVING_LIGHT_CH     2    // Kênh PWM Core 2.x
#define PWM_FREQ                500 // Tần số PWM 5kHz
#define PWM_RESOLUTION          8    // Độ phân giải 8-bit (0 - 255)
#define PWM_MAX_DUTY            255
bool enableLivingLightFading    = true; // Bật hiệu ứng fade on / fade off (sáng dần / tối dần)
#define LIVING_LIGHT_FADE_SPEED 0.18f   // Tốc độ fade tương tự LED_Normal

// --- 2.3 ĐÈN GARAGE (GARAGE LIGHT - MOSFET / PWM) ---
#define PIN_GARAGE_LIGHT        10   // Chân điều khiển đèn garage (GPIO 10 - có thể đổi chân tùy sơ đồ)
#define PWM_GARAGE_LIGHT_CH     4    // Kênh PWM Core 2.x

// --- 2.4 CỬA CHÍNH (SERVO SG90 - MAIN DOOR) ---
#define PIN_SERVO_DOOR          8    // Chân điều khiển tín hiệu PWM Servo SG90 (GPIO 8)
#define PWM_SERVO_CHANNEL       3    // Kênh PWM Core 2.x
#define SERVO_DOOR_ANGLE_CLOSED 90    // Góc đóng cửa: 0 độ
#define SERVO_DOOR_ANGLE_OPEN   0   // Góc mở cửa: 90 độ
// Tốc độ cửa: 0ms mỗi độ -> Cửa mở/đóng tức thì (Max speed, không trễ)
#define DEFAULT_SERVO_STEP_DELAY_MS 0

// --- 2.5 QUẠT THÔNG MINH (SMART FAN - RELAY) ---
#define PIN_FAN                 19   // Chân Relay điều khiển Quạt (GPIO 19)
#define FAN_RELAY_ON            HIGH  // Mức kích Relay Quạt (Module Relay thông dụng kích LOW)
#define FAN_RELAY_OFF           LOW

// --- 2.6 MÁY BƠM NƯỚC (WATER PUMP / MOTOR - RELAY) ---
#define PIN_PUMP                18   // Chân Relay điều khiển Máy bơm nước (GPIO 18)
#define PUMP_RELAY_ON           HIGH  // Mức kích Relay Máy bơm (Module Relay thông dụng kích LOW)
#define PUMP_RELAY_OFF          LOW

// --- 2.7 CẢM BIẾN VÂN TAY AS608 (UART) - ĐĂNG KÝ & MỞ KHÓA CỬA CHÍNH ---
//  AS608 V+  (đỏ)    -> 3V3 ESP32         (KHÔNG cấp 5V nếu module ghi 3.3V)
//  AS608 TX  (vàng)  -> GPIO 16 (RX ESP32)
//  AS608 RX  (trắng) -> GPIO 17 (TX ESP32)
//  AS608 GND (đen)   -> GND ESP32
//  AS608 WAK/TOUCH   -> (tuỳ chọn) GPIO bất kỳ, đặt PIN_FP_TOUCH; -1 = không dùng
//  AS608 Vt / 3.3VT  -> 3V3 (chỉ cần khi dùng chân WAK/TOUCH)
#define ENABLE_FINGERPRINT        true
#define PIN_FP_RX                 16     // ESP32 nhận dữ liệu  <- TX của AS608
#define PIN_FP_TX                 17     // ESP32 gửi dữ liệu   -> RX của AS608
#define PIN_FP_TOUCH              -1     // Chân WAK (touch) của AS608, -1 = không dùng (quét bằng polling)
#define FP_TOUCH_ACTIVE_LEVEL     HIGH   // WAK = HIGH khi có ngón tay đặt lên
#define FP_MIN_CONFIDENCE         50     // Độ tin cậy tối thiểu để chấp nhận mở cửa
#define FP_SCAN_INTERVAL_MS       40     // Chu kỳ quét vân tay khi chờ mở khóa (40ms -> phản hồi tức thì)
#define FP_DOOR_AUTO_CLOSE_MS     5000   // Tự đóng cửa sau khi mở bằng vân tay (0 = không tự đóng)
#define FP_MAX_FAILED_ATTEMPTS    5      // Sai liên tiếp N lần -> khóa quét tạm thời
#define FP_LOCKOUT_MS             30000  // Thời gian khóa quét sau khi sai quá nhiều
#define FP_ENROLL_STEP_TIMEOUT_MS 20000  // Mỗi bước đăng ký chờ tối đa 20s

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

#define FIRMWARE_VERSION        "2.3.0"
#define DEVICE_HUB_NAME         "SYNA-SENSORS-DOOR"

// Topics MQTT chuyên biệt
const String TOPIC_DEVICES_COMMAND        = "hesta/nodes/" NODE_ID "/devices/+/command";
const String TOPIC_NODE_COMMAND           = "hesta/nodes/" NODE_ID "/command";

// 1. Đèn phòng khách
const String TOPIC_LIVING_LIGHT_COMMAND   = "hesta/nodes/" NODE_ID "/devices/" LIVING_LIGHT_DEVICE_ID "/command";
const String TOPIC_LIVING_LIGHT_STATE     = "hesta/nodes/" NODE_ID "/devices/" LIVING_LIGHT_DEVICE_ID "/state";
const String TOPIC_LIVING_LIGHT_ACK       = "hesta/nodes/" NODE_ID "/devices/" LIVING_LIGHT_DEVICE_ID "/ack";
const String TOPIC_LIVING_LIGHT_TELEMETRY = "hesta/nodes/" NODE_ID "/devices/" LIVING_LIGHT_DEVICE_ID "/telemetry";
const String TOPIC_LIVING_LIGHT_STATUS    = "hesta/nodes/" NODE_ID "/devices/" LIVING_LIGHT_DEVICE_ID "/status";

// 2. Đèn garage
const String TOPIC_GARAGE_LIGHT_COMMAND   = "hesta/nodes/" NODE_ID "/devices/" GARAGE_LIGHT_DEVICE_ID "/command";
const String TOPIC_GARAGE_LIGHT_STATE     = "hesta/nodes/" NODE_ID "/devices/" GARAGE_LIGHT_DEVICE_ID "/state";
const String TOPIC_GARAGE_LIGHT_ACK       = "hesta/nodes/" NODE_ID "/devices/" GARAGE_LIGHT_DEVICE_ID "/ack";
const String TOPIC_GARAGE_LIGHT_TELEMETRY = "hesta/nodes/" NODE_ID "/devices/" GARAGE_LIGHT_DEVICE_ID "/telemetry";
const String TOPIC_GARAGE_LIGHT_STATUS    = "hesta/nodes/" NODE_ID "/devices/" GARAGE_LIGHT_DEVICE_ID "/status";

// 3. Cửa chính
const String TOPIC_MAIN_DOOR_COMMAND      = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/command";
const String TOPIC_MAIN_DOOR_STATE        = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/state";
const String TOPIC_MAIN_DOOR_ACK          = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/ack";
const String TOPIC_MAIN_DOOR_TELEMETRY    = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/telemetry";
const String TOPIC_MAIN_DOOR_STATUS       = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/status";
// Sự kiện vân tay (mở khóa thành công/thất bại, đăng ký, xóa) - dùng cho Backend sau này
const String TOPIC_MAIN_DOOR_EVENT        = "hesta/nodes/" NODE_ID "/devices/" MAIN_DOOR_DEVICE_ID "/event";

// 4. Quạt
const String TOPIC_FAN_COMMAND            = "hesta/nodes/" NODE_ID "/devices/" FAN_DEVICE_ID "/command";
const String TOPIC_FAN_STATE              = "hesta/nodes/" NODE_ID "/devices/" FAN_DEVICE_ID "/state";
const String TOPIC_FAN_ACK                = "hesta/nodes/" NODE_ID "/devices/" FAN_DEVICE_ID "/ack";
const String TOPIC_FAN_TELEMETRY          = "hesta/nodes/" NODE_ID "/devices/" FAN_DEVICE_ID "/telemetry";
const String TOPIC_FAN_STATUS             = "hesta/nodes/" NODE_ID "/devices/" FAN_DEVICE_ID "/status";

// 5. Máy bơm nước
const String TOPIC_PUMP_COMMAND           = "hesta/nodes/" NODE_ID "/devices/" PUMP_DEVICE_ID "/command";
const String TOPIC_PUMP_STATE             = "hesta/nodes/" NODE_ID "/devices/" PUMP_DEVICE_ID "/state";
const String TOPIC_PUMP_ACK               = "hesta/nodes/" NODE_ID "/devices/" PUMP_DEVICE_ID "/ack";
const String TOPIC_PUMP_TELEMETRY         = "hesta/nodes/" NODE_ID "/devices/" PUMP_DEVICE_ID "/telemetry";
const String TOPIC_PUMP_STATUS            = "hesta/nodes/" NODE_ID "/devices/" PUMP_DEVICE_ID "/status";

// 6. Cảm biến môi trường & An toàn PCCC
const String TOPIC_LDR_TELEMETRY          = "hesta/nodes/" NODE_ID "/devices/" LDR_DEVICE_ID "/telemetry";
const String TOPIC_LDR_STATUS             = "hesta/nodes/" NODE_ID "/devices/" LDR_DEVICE_ID "/status";

const String TOPIC_PIR_TELEMETRY          = "hesta/nodes/" NODE_ID "/devices/" PIR_DEVICE_ID "/telemetry";
const String TOPIC_PIR_STATUS             = "hesta/nodes/" NODE_ID "/devices/" PIR_DEVICE_ID "/status";

const String TOPIC_DHT_TELEMETRY          = "hesta/nodes/" NODE_ID "/devices/" DHT_DEVICE_ID "/telemetry";
const String TOPIC_DHT_STATUS             = "hesta/nodes/" NODE_ID "/devices/" DHT_DEVICE_ID "/status";

const String TOPIC_FLAME_TELEMETRY        = "hesta/nodes/" NODE_ID "/devices/" FLAME_DEVICE_ID "/telemetry";
const String TOPIC_FLAME_STATUS           = "hesta/nodes/" NODE_ID "/devices/" FLAME_DEVICE_ID "/status";

const String TOPIC_SMOKE_TELEMETRY        = "hesta/nodes/" NODE_ID "/devices/" SMOKE_DEVICE_ID "/telemetry";
const String TOPIC_SMOKE_STATUS           = "hesta/nodes/" NODE_ID "/devices/" SMOKE_DEVICE_ID "/status";

// ====================================================================================================
// 4. CẤU HÌNH BLE PROVISIONING (CHUẨN HỆ SINH THÁI SYNA MOBILE APP)
// ====================================================================================================
#define BLE_SERVICE_UUID   "12345678-1234-1234-1234-123456789000"
#define BLE_COMM_CHAR_UUID "12345678-1234-1234-1234-123456789001"

// ====================================================================================================
// 5. BIẾN TOÀN CỤC VÀ ĐỐI TƯỢNG HỆ THỐNG
// ====================================================================================================
Preferences preferences;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHT dht(PIN_DHT, DHT_TYPE);

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
unsigned long lastFadingUpdate = 0; // Timer cập nhật hiệu ứng fade on / fade off đèn phòng khách

// Chu kỳ gửi Telemetry định kỳ (mỗi 30 giây)
const unsigned long TELEMETRY_INTERVAL_MS = 30000;

// Timers đọc cảm biến
unsigned long lastLDRTime   = 0;
unsigned long lastPIRTime   = 0;
unsigned long lastDHTTime   = 0;
unsigned long lastFlameTime = 0;
unsigned long lastSmokeTime = 0;
const unsigned long LDR_INTERVAL   = 10000;   // 10s
const unsigned long PIR_INTERVAL   = 5000;    // 5s
const unsigned long DHT_INTERVAL   = 30UL * 60UL * 1000UL; // 30 phút (1,800,000 ms)
const unsigned long FLAME_INTERVAL = 5000;    // 5s gửi định kỳ (và gửi TỨC THÌ khi phát hiện lửa)
const unsigned long SMOKE_INTERVAL = 5000;    // 5s gửi định kỳ (và gửi TỨC THÌ khi phát hiện khói)

// Biến lưu trạng thái trước đó để phát hiện cạnh (Edge detection - Báo động khẩn cấp tức thì)
int lastFlameState = -1;
int lastSmokeState = -1;

// --- TRẠNG THÁI ĐÈN PHÒNG KHÁCH (LIVING ROOM LIGHT) ---
bool livingLightPower = false;
uint8_t livingLightBrightness = 100;
uint8_t livingLightLastBrightness = 100;
float livingLightCurrentDuty = 0.0f; // Duty PWM thực tế đang xuất ra chân MOSFET (0.0 - 255.0)
float livingLightTargetDuty = 0.0f;  // Duty PWM mục tiêu cần đạt tới

// --- TRẠNG THÁI ĐÈN GARAGE & TỰ ĐỘNG HÓA CHUYỂN ĐỘNG (PIR AUTO LIGHT) ---
bool garageLightPower = false;
uint8_t garageLightBrightness = 100;
unsigned long garageLightTurnOffTime = 0;              // Thời điểm tắt đèn garage gần nhất (millis)
const unsigned long GARAGE_LIGHT_OFF_IGNORE_WINDOW_MS = 5000; // 5 giây bỏ qua phát hiện chuyển động sau khi tắt đèn garage thủ công
const unsigned long GARAGE_LIGHT_AUTO_OFF_TIMEOUT_MS  = 5000; // 5 giây không có chuyển động thì tự động tắt đèn garage
unsigned long lastGarageMotionTime = 0;                // Thời điểm phát hiện chuyển động gần nhất khi đèn garage sáng (millis)
int lastPIRState = LOW;                                // Trạng thái PIR chu kỳ trước để phát hiện cạnh lên
bool autoGarageLightEnabled = true;                    // Bật/tắt tính năng tự động bật/tắt đèn garage theo chuyển động

// --- TRẠNG THÁI CỬA CHÍNH (SERVO SG90 - MAIN DOOR) & ĐIỀU TỐC SOFT-MOTION ---
bool mainDoorOpenState = false;                          // false = CLOSED (0°), true = OPEN (90°)
int currentServoAngle = SERVO_DOOR_ANGLE_CLOSED;         // Góc hiện tại của Servo (0 - 180 độ)
int targetServoAngle = SERVO_DOOR_ANGLE_CLOSED;          // Góc mục tiêu cần quay tới (0 - 180 độ)
uint16_t servoStepDelayMs = DEFAULT_SERVO_STEP_DELAY_MS; // 0 = mở tức thì, > 0 = soft-motion từng bước
unsigned long lastServoStepTime = 0;                     // Thời điểm thực hiện bước quay gần nhất

// --- TRẠNG THÁI QUẠT THÔNG MINH (SMART FAN) ---
bool fanPower = false;
uint8_t fanSpeed = 1; // Cấp tốc độ quạt: 1 (Thấp), 2 (Trung bình), 3 (Cao)

// --- TRẠNG THÁI MÁY BƠM NƯỚC (WATER PUMP / MOTOR) ---
bool pumpPower = false;

// --- TRẠNG THÁI CẢM BIẾN VÂN TAY AS608 ---
enum FpMode : uint8_t { FP_MODE_SCAN, FP_MODE_ENROLL };
enum FpEnrollStep : uint8_t { FP_EN_WAIT_FINGER_1, FP_EN_WAIT_FINGER_2, FP_EN_WAIT_REMOVE };
bool          fpReady              = false;
bool          fpScanEnabled        = true;
FpMode        fpMode               = FP_MODE_SCAN;
FpEnrollStep  fpEnrollStep         = FP_EN_WAIT_FINGER_1;
FpEnrollStep  fpEnrollAfterRemove  = FP_EN_WAIT_FINGER_1; // Bước tiếp theo sau khi nhấc tay ra
uint16_t      fpEnrollId           = 0;
String        fpEnrollmentId       = "";                  // ID phiên đăng ký từ App/Backend (nếu có)

// ====================================================================================================
// 6. NGUYÊN MẪU HÀM (FORWARD DECLARATIONS)
// ====================================================================================================
void initHardwarePins();

// Đèn phòng khách
void initLivingLightPWM();
void writeLivingLightPWM(uint8_t duty);
void updateLivingLightTargetDuty();
void updateLivingLightSmoothFading();
void applyLivingLightOutput();
void setLivingLightPower(bool power, const char* commandId = nullptr, const String& replyNodeId = "");
void setLivingLightBrightness(int brightness, const char* commandId = nullptr, const String& replyNodeId = "");
void stepLivingLightBrightness(int delta, const char* commandId = nullptr, const String& replyNodeId = "");
void toggleLivingLight(const char* commandId = nullptr);
void publishLivingLightState(const char* commandId = nullptr, const String& replyNodeId = "");
void publishLivingLightTelemetry();

// Đèn garage
void initGarageLightPWM();
void applyGarageLightOutput();
void setGarageLightPower(bool power, const char* commandId = nullptr, const String& replyNodeId = "", bool isAutoOff = false);
void setGarageLightBrightness(int brightness, const char* commandId = nullptr, const String& replyNodeId = "");
void stepGarageLightBrightness(int delta, const char* commandId = nullptr, const String& replyNodeId = "");
void toggleGarageLight(const char* commandId = nullptr);
void publishGarageLightState(const char* commandId = nullptr, const String& replyNodeId = "");
void publishGarageLightTelemetry();

// Cửa chính Servo SG90
void initServoDoor();
void applyServoPwm(int angle);
void setServoAngle(int angle);
void setServoSpeed(uint16_t delayMs);
void updateServoMotion();
void mainDoorOpen(const char* commandId = nullptr, const String& replyNodeId = "");
void mainDoorClose(const char* commandId = nullptr, const String& replyNodeId = "");
void setMainDoorPosition(int angle, const char* commandId = nullptr, const String& replyNodeId = "");
void publishMainDoorState(const char* commandId = nullptr, const String& replyNodeId = "");
void publishMainDoorTelemetry();

// Quạt thông minh
void initFan();
void applyFanOutput();
void setFanPower(bool power, const char* commandId = nullptr, const String& replyNodeId = "");
void toggleFan(const char* commandId = nullptr);
void setFanSpeed(uint8_t speed, const char* commandId = nullptr, const String& replyNodeId = "");
void publishFanState(const char* commandId = nullptr, const String& replyNodeId = "");
void publishFanTelemetry();

// Máy bơm nước
void initPump();
void applyPumpOutput();
void setPumpPower(bool power, const char* commandId = nullptr, const String& replyNodeId = "");
void togglePump(const char* commandId = nullptr);
void publishPumpState(const char* commandId = nullptr, const String& replyNodeId = "");
void publishPumpTelemetry();

// Cảm biến môi trường & An toàn PCCC
bool isDark();
bool isFlameDetected();
bool isSmokeDetected();
void printSensorsSerial();
void readAndPublishLDR();
void readAndPublishPIR();
void readAndPublishDHT();
void readAndPublishFlame();
void readAndPublishSmoke();
void checkFlameAndSmokeAlert();
bool publishSensorMetric(const String& topic, const char* deviceId, const char* metricType, float value, const char* unit);
void checkMotionAndControlLight();

// Flash NVS
void loadNetworkConfigFromNVS();
void saveMQTTConfigToNVS(const String& ipStr, uint16_t port);
void saveWiFiConfigToNVS(const String& ssid, const String& pass);
void loadDeviceStatesFromNVS();
void saveDeviceStatesToNVS();

// BLE
void startBLE();
void notifyBleStatus(const char* status, const char* key1 = nullptr, const char* val1 = nullptr, const char* key2 = nullptr, const char* val2 = nullptr);

// Wi-Fi & MQTT
bool connectWiFi();
bool discoverMQTTBroker();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void processCommandPayload(const String& topicDeviceId, const char* payloadStr, const String& sourceNodeId = "");

// Discovery & Telemetry
void publishAllTelemetries();
void publishNodeDiscovery();

// Serial Monitor
void handleSerialCommands();
void printHelp();
void printStatus();

// Cảm biến vân tay AS608
bool initFingerprint();
void updateFingerprint();
void publishFingerprintEvent(const char* event, int fingerId, int confidence, const char* detail = nullptr, const char* step = nullptr, const char* enrollmentId = nullptr);
void publishFingerprintListEvent();
bool fpIdExists(uint16_t id);
bool fpStartEnroll(int id, const char* enrollmentId = nullptr, String* errStep = nullptr, String* errDetail = nullptr);
void fpEndEnroll(bool success, const char* reason, const char* step = nullptr);
bool fpDeleteById(int id, String* errMsg = nullptr);
void fpEnrollGoRemove(FpEnrollStep next, bool isRetry = false);
void publishMainDoorCommandAck(const char* commandId, const String& replyNodeId, bool success, const char* errorCode = nullptr, const char* message = nullptr);
void printFingerprintHelp();
bool handleFingerprintCommand(const String& lowerCmd);

// ====================================================================================================
// 7. BỘ NHỚ LƯU TRỮ FLASH NVS (PREFERENCES)
// ====================================================================================================
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

void saveDeviceStatesToNVS() {
    preferences.begin("syna_dev", false);
    preferences.putBool("l_light_p", livingLightPower);
    preferences.putUChar("l_light_b", livingLightBrightness);
    preferences.putUChar("l_light_lb", livingLightLastBrightness);
    preferences.putBool("g_light_p", garageLightPower);
    preferences.putUChar("g_light_b", garageLightBrightness);
    preferences.putBool("door_open", mainDoorOpenState);
    preferences.putInt("door_ang", targetServoAngle);
    preferences.putUShort("door_spd", servoStepDelayMs);
    preferences.putBool("fan_p", fanPower);
    preferences.putUChar("fan_s", fanSpeed);
    preferences.putBool("pump_p", pumpPower);
    preferences.putBool("auto_g_light", autoGarageLightEnabled);
    preferences.end();
}

void loadDeviceStatesFromNVS() {
    preferences.begin("syna_dev", true);
    livingLightPower          = preferences.getBool("l_light_p", false);
    livingLightBrightness     = preferences.getUChar("l_light_b", 100);
    livingLightLastBrightness = preferences.getUChar("l_light_lb", (livingLightBrightness > 0) ? livingLightBrightness : 100);
    garageLightPower          = preferences.getBool("g_light_p", false);
    garageLightBrightness     = preferences.getUChar("g_light_b", 100);
    if (garageLightPower) {
        lastGarageMotionTime = millis();
    }
    mainDoorOpenState       = preferences.getBool("door_open", false);
    currentServoAngle       = preferences.getInt("door_ang", mainDoorOpenState ? SERVO_DOOR_ANGLE_OPEN : SERVO_DOOR_ANGLE_CLOSED);
    targetServoAngle        = currentServoAngle;
    servoStepDelayMs        = preferences.getUShort("door_spd", DEFAULT_SERVO_STEP_DELAY_MS);
    fanPower                = preferences.getBool("fan_p", false);
    fanSpeed                = preferences.getUChar("fan_s", 1);
    pumpPower               = preferences.getBool("pump_p", false);
    autoGarageLightEnabled  = preferences.getBool("auto_g_light", true);
    preferences.end();

    // Đảm bảo tốc độ Servo mặc định là 0 (mở tức thì) nếu cài đặt DEFAULT_SERVO_STEP_DELAY_MS = 0
#if DEFAULT_SERVO_STEP_DELAY_MS == 0
    if (servoStepDelayMs != 0) {
        servoStepDelayMs = 0;
        preferences.begin("syna_dev", false);
        preferences.putUShort("door_spd", 0);
        preferences.end();
    }
#endif

    // Áp dụng trạng thái ra phần cứng (Khởi động ban đầu: gán ngay duty thực tế để tránh nhấp nháy)
    updateLivingLightTargetDuty();
    livingLightCurrentDuty = livingLightTargetDuty;
    writeLivingLightPWM((uint8_t)round(livingLightCurrentDuty));

    applyGarageLightOutput();
    applyServoPwm(currentServoAngle);
    applyFanOutput();
    applyPumpOutput();

    Serial.printf("[NVS] Da khoi phuc trang thai: Den phong=%s (%d%%) | Den garage=%s (%d%%) | Cua=%s (%d do, toc do %d ms/do) | Quat=%s | May bom=%s | Auto Garage Light=%s\n",
                  livingLightPower ? "BAT" : "TAT", livingLightBrightness,
                  garageLightPower ? "BAT" : "TAT", garageLightBrightness,
                  mainDoorOpenState ? "OPEN" : "CLOSED", currentServoAngle, servoStepDelayMs,
                  fanPower ? "BAT" : "TAT",
                  pumpPower ? "BAT" : "TAT",
                  autoGarageLightEnabled ? "BAT" : "TAT");
}

// ====================================================================================================
// 8. BLE PROVISIONING (CHUẨN HỆ SINH THÁI SYNA MOBILE APP)
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

        // 3. Reset cấu hình Wi-Fi & MQTT
        if (strcmp(action, "reset_wifi") == 0) {
            preferences.begin("syna_net", false);
            preferences.clear();
            preferences.end();
            WiFi.disconnect(true);
            mqttClient.disconnect();
            mqttBrokerFound = false;
            notifyBleStatus("wifi_reset", "message", "config_cleared");
            Serial.println("[BLE] Da reset toan bo cau hinh mang trong flash NVS.");
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
// 9. KẾT NỐI WIFI & TỰ ĐỘNG DÒ TÌM MQTT BROKER QUA MDNS
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

    String mdnsHost = "syna-sensors-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (!MDNS.begin(mdnsHost.c_str())) {
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
// 10. MQTT CLIENT, PUBLISH VÀ XỬ LÝ LỆNH TỪ BACKEND
// ====================================================================================================
void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!mqttBrokerFound && mqttBrokerIP == INADDR_NONE) {
        discoverMQTTBroker();
    }

    mqttClient.setServer(mqttBrokerIP, mqttBrokerPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(3072); // Buffer lớn nhận và gửi gói JSON đầy đủ (10 thiết bị)
    mqttClient.setSocketTimeout(1); // Tránh để PubSubClient block loop() 15s khi broker offline

    Serial.println();
    Serial.printf("[MQTT] Dang ket noi toi Broker: %s:%d ...\n", mqttBrokerIP.toString().c_str(), mqttBrokerPort);

    String clientId = "HESTA-SENSORS-DOOR-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    // Chuẩn bị LWT (Last Will and Testament) topic cho node
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

        // 1. Subscribe wildcard topic của Node chính
        mqttClient.subscribe(TOPIC_DEVICES_COMMAND.c_str(), 1);
        mqttClient.subscribe(TOPIC_NODE_COMMAND.c_str(), 1);
        Serial.printf("[MQTT SUB] %s\n", TOPIC_DEVICES_COMMAND.c_str());
        Serial.printf("[MQTT SUB] %s\n", TOPIC_NODE_COMMAND.c_str());

        // 2. Subscribe wildcard cho từng Device ID (bất kể Node ID nào từ Backend/App/Test suite gửi tới)
        String subLiving = "hesta/nodes/+/devices/" LIVING_LIGHT_DEVICE_ID "/command";
        String subGarage = "hesta/nodes/+/devices/" GARAGE_LIGHT_DEVICE_ID "/command";
        String subFan    = "hesta/nodes/+/devices/" FAN_DEVICE_ID "/command";
        String subPump   = "hesta/nodes/+/devices/" PUMP_DEVICE_ID "/command";
        String subDoor   = "hesta/nodes/+/devices/" MAIN_DOOR_DEVICE_ID "/command";
        mqttClient.subscribe(subLiving.c_str(), 1);
        mqttClient.subscribe(subGarage.c_str(), 1);
        mqttClient.subscribe(subFan.c_str(), 1);
        mqttClient.subscribe(subPump.c_str(), 1);
        mqttClient.subscribe(subDoor.c_str(), 1);

        // 3. Hỗ trợ thêm Node ID phụ (tương thích test-suite fan.http & water-pump.http)
        mqttClient.subscribe("hesta/nodes/664017ce-c045-4b0e-9a26-f9f95f996803/devices/+/command", 1);
        mqttClient.subscribe("hesta/nodes/664017ce-c045-4b0e-9a26-f9f95f996803/command", 1);

        // Báo trạng thái ONLINE cho toàn bộ 10 thiết bị con
        mqttClient.publish(TOPIC_MAIN_DOOR_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_LIVING_LIGHT_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_GARAGE_LIGHT_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_FAN_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_PUMP_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_LDR_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_PIR_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_DHT_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_FLAME_STATUS.c_str(), "ONLINE", true);
        mqttClient.publish(TOPIC_SMOKE_STATUS.c_str(), "ONLINE", true);

        // Báo State ban đầu của các thiết bị chấp hành
        publishMainDoorState();
        publishLivingLightState();
        publishGarageLightState();
        publishFanState();
        publishPumpState();

        // Gửi Telemetry ban đầu và gói Node Discovery
        publishAllTelemetries();
        readAndPublishDHT(); // Gửi ngay giá trị DHT lần đầu khi vừa kết nối Broker
        publishNodeDiscovery();
    } else {
        Serial.printf("[MQTT ERROR] Ket noi that bai, ma loi rc=%d. Thu lai sau...\n", mqttClient.state());
    }
}

/**
 * Gửi trạng thái Cửa chính (Main Door - Servo SG90)
 */
void publishMainDoorState(const char* commandId, const String& replyNodeId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]       = NODE_ID;
    doc["deviceId"]     = MAIN_DOOR_DEVICE_ID;
    doc["deviceType"]   = DEVICE_TYPE_DOOR;
    doc["device"]       = "main_door";
    doc["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
    doc["open"]         = mainDoorOpenState;
    doc["angle"]        = targetServoAngle;
    doc["currentAngle"] = currentServoAngle;
    doc["speed"]        = servoStepDelayMs;

    JsonObject st = doc["state"].to<JsonObject>();
    st["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
    st["open"]         = mainDoorOpenState;
    st["angle"]        = targetServoAngle;
    st["currentAngle"] = currentServoAngle;
    st["speed"]        = servoStepDelayMs;

    JsonObject curSt = doc["currentState"].to<JsonObject>();
    curSt["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
    curSt["open"]         = mainDoorOpenState;
    curSt["angle"]        = targetServoAngle;
    curSt["currentAngle"] = currentServoAngle;
    curSt["speed"]        = servoStepDelayMs;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_MAIN_DOOR_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> MAIN DOOR]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = MAIN_DOOR_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
        ackSt["open"]         = mainDoorOpenState;
        ackSt["angle"]        = targetServoAngle;
        ackSt["currentAngle"] = currentServoAngle;
        ackSt["speed"]        = servoStepDelayMs;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_MAIN_DOOR_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> MAIN DOOR]: %s\n", ackBuf);
    }

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altStateTopic = "hesta/nodes/" + replyNodeId + "/devices/" MAIN_DOOR_DEVICE_ID "/state";
        mqttClient.publish(altStateTopic.c_str(), buf, true);
        if (commandId && strlen(commandId) > 0) {
            String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" MAIN_DOOR_DEVICE_ID "/ack";
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument ackDoc2;
#else
            DynamicJsonDocument ackDoc2(512);
#endif
            ackDoc2["commandId"] = commandId;
            ackDoc2["deviceId"]  = MAIN_DOOR_DEVICE_ID;
            ackDoc2["status"]    = "SUCCESS";
            JsonObject ackSt2 = ackDoc2["state"].to<JsonObject>();
            ackSt2["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
            ackSt2["open"]         = mainDoorOpenState;
            ackSt2["angle"]        = targetServoAngle;
            ackSt2["currentAngle"] = currentServoAngle;
            ackSt2["speed"]        = servoStepDelayMs;
            char ackBuf2[512];
            serializeJson(ackDoc2, ackBuf2, sizeof(ackBuf2));
            mqttClient.publish(altAckTopic.c_str(), ackBuf2, false);
        }
    }
}

void publishMainDoorCommandAck(const char* commandId, const String& replyNodeId, bool success, const char* errorCode, const char* message) {
    if (!mqttClient.connected() || !commandId || strlen(commandId) == 0) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument ackDoc;
#else
    DynamicJsonDocument ackDoc(512);
#endif
    ackDoc["commandId"] = commandId;
    ackDoc["deviceId"]  = MAIN_DOOR_DEVICE_ID;
    ackDoc["status"]    = success ? "SUCCESS" : "FAILED";
    if (errorCode && strlen(errorCode) > 0) ackDoc["errorCode"] = errorCode;
    if (message && strlen(message) > 0)     ackDoc["message"]   = message;

    char ackBuf[512];
    serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
    mqttClient.publish(TOPIC_MAIN_DOOR_ACK.c_str(), ackBuf, false);
    Serial.printf("[MQTT ACK -> MAIN DOOR FP]: %s\n", ackBuf);

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" MAIN_DOOR_DEVICE_ID "/ack";
        mqttClient.publish(altAckTopic.c_str(), ackBuf, false);
    }
}

void publishMainDoorTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]       = NODE_ID;
    doc["deviceId"]     = MAIN_DOOR_DEVICE_ID;
    doc["deviceType"]   = DEVICE_TYPE_DOOR;
    doc["device"]       = "main_door";
    doc["state"]        = mainDoorOpenState ? "OPEN" : "CLOSED";
    doc["open"]         = mainDoorOpenState;
    doc["angle"]        = targetServoAngle;
    doc["currentAngle"] = currentServoAngle;
    doc["speed"]        = servoStepDelayMs;
    doc["uptime"]       = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_MAIN_DOOR_TELEMETRY.c_str(), buf, false);
}

/**
 * Gửi trạng thái Đèn phòng khách (Living Room Light - b016fe3d-36fc-4d3e-a8bd-e287d7138395)
 */
void publishLivingLightState(const char* commandId, const String& replyNodeId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = LIVING_LIGHT_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["device"]     = "living_room_light";
    doc["name"]       = "Đèn phòng khách";
    doc["power"]      = livingLightPower;
    doc["brightness"] = livingLightBrightness;

    JsonObject st = doc["state"].to<JsonObject>();
    st["power"]      = livingLightPower;
    st["brightness"] = livingLightBrightness;

    JsonObject curSt = doc["currentState"].to<JsonObject>();
    curSt["power"]      = livingLightPower;
    curSt["brightness"] = livingLightBrightness;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_LIVING_LIGHT_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> LIVING LIGHT]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = LIVING_LIGHT_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["power"]      = livingLightPower;
        ackSt["brightness"] = livingLightBrightness;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_LIVING_LIGHT_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> LIVING LIGHT]: %s\n", ackBuf);
    }

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altStateTopic = "hesta/nodes/" + replyNodeId + "/devices/" LIVING_LIGHT_DEVICE_ID "/state";
        mqttClient.publish(altStateTopic.c_str(), buf, true);
        if (commandId && strlen(commandId) > 0) {
            String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" LIVING_LIGHT_DEVICE_ID "/ack";
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument ackDoc2;
#else
            DynamicJsonDocument ackDoc2(512);
#endif
            ackDoc2["commandId"] = commandId;
            ackDoc2["deviceId"]  = LIVING_LIGHT_DEVICE_ID;
            ackDoc2["status"]    = "SUCCESS";
            JsonObject ackSt2 = ackDoc2["state"].to<JsonObject>();
            ackSt2["power"]      = livingLightPower;
            ackSt2["brightness"] = livingLightBrightness;
            char ackBuf2[512];
            serializeJson(ackDoc2, ackBuf2, sizeof(ackBuf2));
            mqttClient.publish(altAckTopic.c_str(), ackBuf2, false);
        }
    }
}

void publishLivingLightTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = LIVING_LIGHT_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["device"]     = "living_room_light";
    doc["power"]      = livingLightPower;
    doc["brightness"] = livingLightBrightness;
    doc["duty"]       = (livingLightPower && livingLightBrightness > 0) ? (int)(((uint16_t)livingLightBrightness * 255) / 100) : 0;
    doc["uptime"]     = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_LIVING_LIGHT_TELEMETRY.c_str(), buf, false);
}

/**
 * Gửi trạng thái Đèn garage (Garage Light - f23a13d8-1498-499d-aac2-04cee145f020)
 */
void publishGarageLightState(const char* commandId, const String& replyNodeId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = GARAGE_LIGHT_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["device"]     = "garage_light";
    doc["name"]       = "Đèn garage";
    doc["power"]      = garageLightPower;
    doc["brightness"] = garageLightBrightness;
    doc["autoLight"]  = autoGarageLightEnabled;

    JsonObject st = doc["state"].to<JsonObject>();
    st["power"]      = garageLightPower;
    st["brightness"] = garageLightBrightness;
    st["autoLight"]  = autoGarageLightEnabled;

    JsonObject curSt = doc["currentState"].to<JsonObject>();
    curSt["power"]      = garageLightPower;
    curSt["brightness"] = garageLightBrightness;
    curSt["autoLight"]  = autoGarageLightEnabled;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_GARAGE_LIGHT_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> GARAGE LIGHT]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = GARAGE_LIGHT_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["power"]      = garageLightPower;
        ackSt["brightness"] = garageLightBrightness;
        ackSt["autoLight"]  = autoGarageLightEnabled;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_GARAGE_LIGHT_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> GARAGE LIGHT]: %s\n", ackBuf);
    }

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altStateTopic = "hesta/nodes/" + replyNodeId + "/devices/" GARAGE_LIGHT_DEVICE_ID "/state";
        mqttClient.publish(altStateTopic.c_str(), buf, true);
        if (commandId && strlen(commandId) > 0) {
            String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" GARAGE_LIGHT_DEVICE_ID "/ack";
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument ackDoc2;
#else
            DynamicJsonDocument ackDoc2(512);
#endif
            ackDoc2["commandId"] = commandId;
            ackDoc2["deviceId"]  = GARAGE_LIGHT_DEVICE_ID;
            ackDoc2["status"]    = "SUCCESS";
            JsonObject ackSt2 = ackDoc2["state"].to<JsonObject>();
            ackSt2["power"]      = garageLightPower;
            ackSt2["brightness"] = garageLightBrightness;
            ackSt2["autoLight"]  = autoGarageLightEnabled;
            char ackBuf2[512];
            serializeJson(ackDoc2, ackBuf2, sizeof(ackBuf2));
            mqttClient.publish(altAckTopic.c_str(), ackBuf2, false);
        }
    }
}

void publishGarageLightTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = GARAGE_LIGHT_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_LIGHT;
    doc["device"]     = "garage_light";
    doc["power"]      = garageLightPower;
    doc["brightness"] = garageLightBrightness;
    doc["duty"]       = (garageLightPower && garageLightBrightness > 0) ? (int)(((uint16_t)garageLightBrightness * 255) / 100) : 0;
    doc["autoLight"]  = autoGarageLightEnabled;
    doc["uptime"]     = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_GARAGE_LIGHT_TELEMETRY.c_str(), buf, false);
}

/**
 * Gửi trạng thái Quạt thông minh (Smart Fan)
 */
void publishFanState(const char* commandId, const String& replyNodeId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = FAN_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_FAN;
    doc["device"]     = "fan";
    doc["power"]      = fanPower;
    doc["state"]      = fanPower ? "ON" : "OFF";
    doc["speed"]      = fanSpeed;

    JsonObject st = doc["state"].to<JsonObject>();
    st["power"] = fanPower;
    st["state"] = fanPower ? "ON" : "OFF";
    st["speed"] = fanSpeed;

    JsonObject curSt = doc["currentState"].to<JsonObject>();
    curSt["power"] = fanPower;
    curSt["state"] = fanPower ? "ON" : "OFF";
    curSt["speed"] = fanSpeed;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_FAN_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> FAN]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = FAN_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["power"] = fanPower;
        ackSt["state"] = fanPower ? "ON" : "OFF";
        ackSt["speed"] = fanSpeed;

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_FAN_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> FAN]: %s\n", ackBuf);
    }

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altStateTopic = "hesta/nodes/" + replyNodeId + "/devices/" FAN_DEVICE_ID "/state";
        mqttClient.publish(altStateTopic.c_str(), buf, true);
        if (commandId && strlen(commandId) > 0) {
            String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" FAN_DEVICE_ID "/ack";
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument ackDoc2;
#else
            DynamicJsonDocument ackDoc2(512);
#endif
            ackDoc2["commandId"] = commandId;
            ackDoc2["deviceId"]  = FAN_DEVICE_ID;
            ackDoc2["status"]    = "SUCCESS";
            JsonObject ackSt2 = ackDoc2["state"].to<JsonObject>();
            ackSt2["power"] = fanPower;
            ackSt2["state"] = fanPower ? "ON" : "OFF";
            ackSt2["speed"] = fanSpeed;
            char ackBuf2[512];
            serializeJson(ackDoc2, ackBuf2, sizeof(ackBuf2));
            mqttClient.publish(altAckTopic.c_str(), ackBuf2, false);
        }
    }
}

void publishFanTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = FAN_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_FAN;
    doc["power"]      = fanPower;
    doc["state"]      = fanPower ? "ON" : "OFF";
    doc["speed"]      = fanSpeed;
    doc["uptime"]     = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_FAN_TELEMETRY.c_str(), buf, false);
}

/**
 * Gửi trạng thái Máy bơm nước (Water Pump / Motor)
 */
void publishPumpState(const char* commandId, const String& replyNodeId) {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = PUMP_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_PUMP;
    doc["device"]     = "pump";
    doc["power"]      = pumpPower;
    doc["state"]      = pumpPower ? "ON" : "OFF";

    JsonObject st = doc["state"].to<JsonObject>();
    st["power"] = pumpPower;
    st["state"] = pumpPower ? "ON" : "OFF";

    JsonObject curSt = doc["currentState"].to<JsonObject>();
    curSt["power"] = pumpPower;
    curSt["state"] = pumpPower ? "ON" : "OFF";

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_PUMP_STATE.c_str(), buf, true);
    Serial.printf("[MQTT STATE -> PUMP]: %s\n", buf);

    if (commandId && strlen(commandId) > 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument ackDoc;
#else
        DynamicJsonDocument ackDoc(512);
#endif
        ackDoc["commandId"] = commandId;
        ackDoc["deviceId"]  = PUMP_DEVICE_ID;
        ackDoc["status"]    = "SUCCESS";
        JsonObject ackSt = ackDoc["state"].to<JsonObject>();
        ackSt["power"] = pumpPower;
        ackSt["state"] = pumpPower ? "ON" : "OFF";

        char ackBuf[512];
        serializeJson(ackDoc, ackBuf, sizeof(ackBuf));
        mqttClient.publish(TOPIC_PUMP_ACK.c_str(), ackBuf, false);
        Serial.printf("[MQTT ACK -> PUMP]: %s\n", ackBuf);
    }

    if (replyNodeId.length() > 0 && replyNodeId != NODE_ID) {
        String altStateTopic = "hesta/nodes/" + replyNodeId + "/devices/" PUMP_DEVICE_ID "/state";
        mqttClient.publish(altStateTopic.c_str(), buf, true);
        if (commandId && strlen(commandId) > 0) {
            String altAckTopic = "hesta/nodes/" + replyNodeId + "/devices/" PUMP_DEVICE_ID "/ack";
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument ackDoc2;
#else
            DynamicJsonDocument ackDoc2(512);
#endif
            ackDoc2["commandId"] = commandId;
            ackDoc2["deviceId"]  = PUMP_DEVICE_ID;
            ackDoc2["status"]    = "SUCCESS";
            JsonObject ackSt2 = ackDoc2["state"].to<JsonObject>();
            ackSt2["power"] = pumpPower;
            ackSt2["state"] = pumpPower ? "ON" : "OFF";
            char ackBuf2[512];
            serializeJson(ackDoc2, ackBuf2, sizeof(ackBuf2));
            mqttClient.publish(altAckTopic.c_str(), ackBuf2, false);
        }
    }
}

void publishPumpTelemetry() {
    if (!mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = PUMP_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_PUMP;
    doc["power"]      = pumpPower;
    doc["state"]      = pumpPower ? "ON" : "OFF";
    doc["uptime"]     = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_PUMP_TELEMETRY.c_str(), buf, false);
}

/**
 * Gửi thông số cảm biến môi trường (LDR, PIR, DHT22)
 */
bool publishSensorMetric(const String& topic, const char* deviceId, const char* metricType, float value, const char* unit) {
    if (!mqttClient.connected()) return false;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(256);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = deviceId;
    doc["metricType"] = metricType;
    doc["value"]      = value;
    doc["unit"]       = unit;
    doc["uptime"]     = millis() / 1000;

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    bool success = mqttClient.publish(topic.c_str(), payload, false);
    Serial.printf("[SENSOR TELEMETRY -> %s]: %s\n", metricType, payload);
    return success;
}

void publishAllTelemetries() {
    publishLivingLightTelemetry();
    publishGarageLightTelemetry();
    publishMainDoorTelemetry();
    publishFanTelemetry();
    publishPumpTelemetry();
    readAndPublishLDR();
    readAndPublishPIR();
    readAndPublishFlame();
    readAndPublishSmoke();
    // Ghi chú: DHT22 được đọc và gửi theo chu kỳ riêng DHT_INTERVAL = 30 phút (tránh gửi dồn dập mỗi 30s)
}

/**
 * Gửi gói Discovery đăng ký Node và toàn bộ 8 thiết bị trong hệ thống
 */
void publishNodeDiscovery() {
    if (!mqttClient.connected()) return;

    String topic = "hesta/nodes/" NODE_ID "/discovery";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif

    doc["nodeId"]          = NODE_ID;
    doc["deviceName"]      = DEVICE_HUB_NAME;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["macAddress"]      = WiFi.macAddress();
    doc["ipAddress"]       = WiFi.localIP().toString();
    doc["status"]          = "ONLINE";

    JsonArray devList = doc["devices"].to<JsonArray>();

    // 1. Cửa chính (Main Door - Servo SG90)
    JsonObject dDoor = devList.add<JsonObject>();
    dDoor["deviceId"] = MAIN_DOOR_DEVICE_ID;
    dDoor["name"]     = "Cửa chính";
    dDoor["type"]     = DEVICE_TYPE_DOOR;
    dDoor["state"]    = mainDoorOpenState ? "OPEN" : "CLOSED";
    dDoor["angle"]    = currentServoAngle;
    dDoor["speed"]    = servoStepDelayMs;
    dDoor["gpio"]     = PIN_SERVO_DOOR;

    // 2. Đèn phòng khách (Living Room Light)
    JsonObject dLiving = devList.add<JsonObject>();
    dLiving["deviceId"]   = LIVING_LIGHT_DEVICE_ID;
    dLiving["name"]       = "Đèn phòng khách";
    dLiving["type"]       = DEVICE_TYPE_LIGHT;
    dLiving["power"]      = livingLightPower;
    dLiving["brightness"] = livingLightBrightness;
    dLiving["gpio"]       = PIN_LIVING_LIGHT;

    // 3. Đèn garage (Garage Light - Bật khi có chuyển động)
    JsonObject dGarage = devList.add<JsonObject>();
    dGarage["deviceId"]   = GARAGE_LIGHT_DEVICE_ID;
    dGarage["name"]       = "Đèn garage";
    dGarage["type"]       = DEVICE_TYPE_LIGHT;
    dGarage["power"]      = garageLightPower;
    dGarage["brightness"] = garageLightBrightness;
    dGarage["autoLight"]  = autoGarageLightEnabled;
    dGarage["gpio"]       = PIN_GARAGE_LIGHT;

    // 4. Quạt thông minh (Smart Fan)
    JsonObject dFan = devList.add<JsonObject>();
    dFan["deviceId"] = FAN_DEVICE_ID;
    dFan["name"]     = "Quạt";
    dFan["type"]     = DEVICE_TYPE_FAN;
    dFan["power"]    = fanPower;
    dFan["speed"]    = fanSpeed;
    dFan["gpio"]     = PIN_FAN;

    // 5. Máy bơm nước (Water Pump / Motor)
    JsonObject dPump = devList.add<JsonObject>();
    dPump["deviceId"] = PUMP_DEVICE_ID;
    dPump["name"]     = "Máy bơm nước";
    dPump["type"]     = DEVICE_TYPE_PUMP;
    dPump["power"]    = pumpPower;
    dPump["gpio"]     = PIN_PUMP;

    // 6. Cảm biến ánh sáng LDR
    JsonObject dLdr = devList.add<JsonObject>();
    dLdr["deviceId"] = LDR_DEVICE_ID;
    dLdr["name"]     = "Cảm biến ánh sáng";
    dLdr["type"]     = DEVICE_TYPE_LDR;
    dLdr["gpio"]     = PIN_LDR;

    // 7. Cảm biến chuyển động PIR
    JsonObject dPir = devList.add<JsonObject>();
    dPir["deviceId"] = PIR_DEVICE_ID;
    dPir["name"]     = "Cảm biến chuyển động";
    dPir["type"]     = DEVICE_TYPE_PIR;
    dPir["gpio"]     = PIN_PIR;

    // 8. Cảm biến nhiệt ẩm DHT22
    JsonObject dDht = devList.add<JsonObject>();
    dDht["deviceId"] = DHT_DEVICE_ID;
    dDht["name"]     = "Cảm biến nhiệt ẩm";
    dDht["type"]     = DEVICE_TYPE_DHT;
    dDht["gpio"]     = PIN_DHT;

    // 9. Cảm biến lửa Flame Sensor
    JsonObject dFlame = devList.add<JsonObject>();
    dFlame["deviceId"] = FLAME_DEVICE_ID;
    dFlame["name"]     = "Cảm biến lửa";
    dFlame["type"]     = DEVICE_TYPE_FLAME;
    dFlame["gpio"]     = PIN_FLAME;

    // 10. Cảm biến khói MH MQ Sensor
    JsonObject dSmoke = devList.add<JsonObject>();
    dSmoke["deviceId"] = SMOKE_DEVICE_ID;
    dSmoke["name"]     = "Cảm biến khói";
    dSmoke["type"]     = DEVICE_TYPE_SMOKE;
    dSmoke["gpio"]     = PIN_SMOKE;

    char buffer[2560];
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
    String sourceNodeId = "";

    // Phân tích Node ID từ Topic: hesta/nodes/{nodeId}/...
    int nodesIdx = topicStr.indexOf("hesta/nodes/");
    if (nodesIdx >= 0) {
        int startNode = nodesIdx + 12;
        int nextSlash = topicStr.indexOf('/', startNode);
        if (nextSlash > startNode) {
            sourceNodeId = topicStr.substring(startNode, nextSlash);
        }
    }

    // Phân tích Device ID từ Topic: hesta/nodes/{nodeId}/devices/{deviceId}/command
    int devIdx = topicStr.indexOf("/devices/");
    if (devIdx >= 0) {
        int startPos = devIdx + 9;
        int endPos = topicStr.indexOf("/command", startPos);
        if (endPos > startPos) {
            targetDeviceId = topicStr.substring(startPos, endPos);
        }
    }

    processCommandPayload(targetDeviceId, message, sourceNodeId);
}

/**
 * Xử lý lệnh điều khiển thiết bị (Cửa chính, Đèn phòng khách, Đèn garage, Quạt, Máy bơm nước)
 */
void processCommandPayload(const String& topicDeviceId, const char* payloadStr, const String& sourceNodeId) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    DeserializationError error = deserializeJson(doc, payloadStr);
    if (error) {
        // Fallback xử lý lệnh text thuần túy
        String rawAction = String(payloadStr);
        rawAction.trim();
        rawAction.toUpperCase();

        if (rawAction == "OPEN") { mainDoorOpen(nullptr, sourceNodeId); return; }
        if (rawAction == "CLOSE") { mainDoorClose(nullptr, sourceNodeId); return; }
        if (rawAction == "STOP") { targetServoAngle = currentServoAngle; publishMainDoorState(nullptr, sourceNodeId); return; }
        if (rawAction == "ON" || rawAction == "BAT" || rawAction == "LIVING_ON") { setLivingLightPower(true, nullptr, sourceNodeId); return; }
        if (rawAction == "OFF" || rawAction == "TAT" || rawAction == "LIVING_OFF") { setLivingLightPower(false, nullptr, sourceNodeId); return; }
        if (rawAction == "GARAGE_ON") { setGarageLightPower(true, nullptr, sourceNodeId); return; }
        if (rawAction == "GARAGE_OFF") { setGarageLightPower(false, nullptr, sourceNodeId); return; }
        if (rawAction == "FAN_ON") { setFanPower(true, nullptr, sourceNodeId); return; }
        if (rawAction == "FAN_OFF") { setFanPower(false, nullptr, sourceNodeId); return; }
        if (rawAction == "PUMP_ON" || rawAction == "MOTOR_ON") { setPumpPower(true, nullptr, sourceNodeId); return; }
        if (rawAction == "PUMP_OFF" || rawAction == "MOTOR_OFF") { setPumpPower(false, nullptr, sourceNodeId); return; }
        if (rawAction == "ALL_ON") { 
            setLivingLightPower(true, nullptr, sourceNodeId);
            setGarageLightPower(true, nullptr, sourceNodeId);
            setFanPower(true, nullptr, sourceNodeId); 
            setPumpPower(true, nullptr, sourceNodeId); 
            return; 
        }
        if (rawAction == "ALL_OFF") { 
            setLivingLightPower(false, nullptr, sourceNodeId);
            setGarageLightPower(false, nullptr, sourceNodeId);
            setFanPower(false, nullptr, sourceNodeId); 
            setPumpPower(false, nullptr, sourceNodeId); 
            return; 
        }

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

    // Parse tham số Power
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

    // Parse tham số Brightness
    auto parseBrightnessNumber = [](JsonVariant v) -> int {
        if (v.isNull()) return -1;
        if (v.is<float>() || v.is<double>()) {
            float f = v.as<float>();
            if (f > 0.0f && f <= 1.0f) return (int)round(f * 100.0f);
            return (int)round(f);
        }
        if (v.is<int>()) return v.as<int>();
        if (v.is<const char*>() || v.is<String>()) {
            String s = v.as<String>();
            s.trim();
            if (s.endsWith("%")) s = s.substring(0, s.length() - 1);
            float f = s.toFloat();
            if (f > 0.0f && f <= 1.0f && s.indexOf('.') >= 0) return (int)round(f * 100.0f);
            return (int)round(f);
        }
        return -1;
    };

    bool hasBrightnessParam = false;
    int brightnessValue = -1;
    const char* briKeys[] = {"brightness", "level", "value", "val", "dim", "percent"};
    for (const char* k : briKeys) {
        if (doc.containsKey(k)) {
            int parsed = parseBrightnessNumber(doc[k]);
            if (parsed >= 0) { hasBrightnessParam = true; brightnessValue = constrain(parsed, 0, 100); break; }
        }
        if (!params.isNull() && params.containsKey(k)) {
            int parsed = parseBrightnessNumber(params[k]);
            if (parsed >= 0) { hasBrightnessParam = true; brightnessValue = constrain(parsed, 0, 100); break; }
        }
    }

    // Parse tham số bước tăng giảm
    int stepAmount = 10;
    const char* stepKeys[] = {"amount", "step", "delta", "stepAmount"};
    for (const char* sk : stepKeys) {
        if (doc.containsKey(sk)) {
            int sVal = doc[sk].as<int>();
            if (sVal != 0) { stepAmount = abs(sVal); break; }
        }
        if (!params.isNull() && params.containsKey(sk)) {
            int sVal = params[sk].as<int>();
            if (sVal != 0) { stepAmount = abs(sVal); break; }
        }
    }

    // Parse tham số Cửa chính
    int cmdSpeed = -1;
    if (doc.containsKey("speed")) cmdSpeed = doc["speed"].as<int>();
    else if (doc.containsKey("delay")) cmdSpeed = doc["delay"].as<int>();
    else if (!params.isNull() && params.containsKey("speed")) cmdSpeed = params["speed"].as<int>();

    int cmdAngle = -1;
    if (doc.containsKey("angle")) cmdAngle = doc["angle"].as<int>();
    else if (doc.containsKey("position")) cmdAngle = doc["position"].as<int>();
    else if (!params.isNull()) {
        if (params.containsKey("angle")) cmdAngle = params["angle"].as<int>();
        else if (params.containsKey("position")) cmdAngle = params["position"].as<int>();
    }

    // Kiểm tra trực tiếp các trường riêng biệt: {"living": true, "garage": true, "fan": true, "pump": true}
    bool handledDirectField = false;
    if (doc.containsKey("living_light") || doc.containsKey("living")) {
        handledDirectField = true;
        JsonVariant v = doc.containsKey("living_light") ? doc["living_light"] : doc["living"];
        bool b = v.is<bool>() ? v.as<bool>() : (v.as<String>() == "ON" || v.as<String>() == "1" || v.as<String>() == "true");
        setLivingLightPower(b, commandId, sourceNodeId);
    }
    if (doc.containsKey("garage_light") || doc.containsKey("garage")) {
        handledDirectField = true;
        JsonVariant v = doc.containsKey("garage_light") ? doc["garage_light"] : doc["garage"];
        bool b = v.is<bool>() ? v.as<bool>() : (v.as<String>() == "ON" || v.as<String>() == "1" || v.as<String>() == "true");
        setGarageLightPower(b, commandId, sourceNodeId);
    }
    if (doc.containsKey("fan")) {
        handledDirectField = true;
        bool f = doc["fan"].is<bool>() ? doc["fan"].as<bool>() : (doc["fan"].as<String>() == "ON" || doc["fan"].as<String>() == "1" || doc["fan"].as<String>() == "true");
        setFanPower(f, commandId, sourceNodeId);
    }
    if (doc.containsKey("motor") || doc.containsKey("pump")) {
        handledDirectField = true;
        JsonVariant v = doc.containsKey("pump") ? doc["pump"] : doc["motor"];
        bool m = v.is<bool>() ? v.as<bool>() : (v.as<String>() == "ON" || v.as<String>() == "1" || v.as<String>() == "true");
        setPumpPower(m, commandId, sourceNodeId);
    }

    // 4. Phân loại thiết bị mục tiêu
    bool isMainDoor = false;
    bool isLivingLight = false;
    bool isGarageLight = false;
    bool isFan = false;
    bool isPump = false;
    bool isAll = false;

    // Kiểm tra theo Device ID
    if (deviceId.equalsIgnoreCase(MAIN_DOOR_DEVICE_ID) || deviceId.equalsIgnoreCase("main_door") || 
        deviceId.equalsIgnoreCase("door") || deviceId.equalsIgnoreCase("servo") || deviceId.equalsIgnoreCase("cua_chinh")) {
        isMainDoor = true;
    } else if (deviceId.equalsIgnoreCase(LIVING_LIGHT_DEVICE_ID) || deviceId.equalsIgnoreCase("living_light") || 
               deviceId.equalsIgnoreCase("living_room_light") || deviceId.equalsIgnoreCase("den_phong_khach") || 
               deviceId.equalsIgnoreCase("living")) {
        isLivingLight = true;
    } else if (deviceId.equalsIgnoreCase(GARAGE_LIGHT_DEVICE_ID) || deviceId.equalsIgnoreCase("garage_light") || 
               deviceId.equalsIgnoreCase("den_garage") || deviceId.equalsIgnoreCase("garage")) {
        isGarageLight = true;
    } else if (deviceId.equalsIgnoreCase(FAN_DEVICE_ID) || deviceId.equalsIgnoreCase("fan") || 
               deviceId.equalsIgnoreCase("quat")) {
        isFan = true;
    } else if (deviceId.equalsIgnoreCase(PUMP_DEVICE_ID) || deviceId.equalsIgnoreCase("pump") || 
               deviceId.equalsIgnoreCase("water_pump") || deviceId.equalsIgnoreCase("motor") || 
               deviceId.equalsIgnoreCase("may_bom") || deviceId.equalsIgnoreCase("bom")) {
        isPump = true;
    } else {
        // Phân loại theo target
        String target = "";
        if (doc.containsKey("target")) target = doc["target"].as<String>();
        else if (!params.isNull() && params.containsKey("target")) target = params["target"].as<String>();

        if (target.equalsIgnoreCase("garage") || target.equalsIgnoreCase("garage_light") || target.equalsIgnoreCase("den_garage")) {
            isGarageLight = true;
        } else if (target.equalsIgnoreCase("living") || target.equalsIgnoreCase("living_light") || 
                   target.equalsIgnoreCase("den_phong_khach") || target.equalsIgnoreCase("light") || target.equalsIgnoreCase("den")) {
            isLivingLight = true;
        } else if (target.equalsIgnoreCase("fan") || target.equalsIgnoreCase("quat")) {
            isFan = true;
        } else if (target.equalsIgnoreCase("pump") || target.equalsIgnoreCase("water_pump") || target.equalsIgnoreCase("motor")) {
            isPump = true;
        } else if (target.equalsIgnoreCase("main_door") || target.equalsIgnoreCase("door") || target.equalsIgnoreCase("servo")) {
            isMainDoor = true;
        } else if (target.equalsIgnoreCase("all") || target.equalsIgnoreCase("tat_ca")) {
            isAll = true;
        } else {
            // Phân loại theo Action
            if (action == "ALL_ON" || action == "ALL_OFF") {
                isAll = true;
            } else if (action.indexOf("GARAGE") >= 0) {
                isGarageLight = true;
            } else if (action.indexOf("LIVING") >= 0 || action.indexOf("PHONG_KHACH") >= 0) {
                isLivingLight = true;
            } else if (action.indexOf("FAN") >= 0 || action.indexOf("QUAT") >= 0) {
                isFan = true;
            } else if (action.indexOf("PUMP") >= 0 || action.indexOf("MOTOR") >= 0 || action.indexOf("BOM") >= 0) {
                isPump = true;
            } else if (action.indexOf("MAIN_DOOR") >= 0 || action.indexOf("SERVO") >= 0 || action.indexOf("DOOR") >= 0 ||
                       action == "OPEN" || action == "CLOSE" || action == "MO" || action == "DONG" || action == "STOP") {
                isMainDoor = true;
            } else if (action.indexOf("LIGHT") >= 0 || action.indexOf("LAMP") >= 0 || action.indexOf("DEN") >= 0 ||
                       action.indexOf("BRIGHTNESS") >= 0 || action.indexOf("DIM") >= 0) {
                isLivingLight = true; // Mặc định light chung về Living Room
            } else {
                if (hasBrightnessParam) isLivingLight = true;
                else if (cmdAngle >= 0) isMainDoor = true;
                else if (doc.containsKey("fan")) isFan = true;
                else if (doc.containsKey("motor") || doc.containsKey("pump")) isPump = true;
                else if (handledDirectField) return;
                else isLivingLight = true;
            }
        }
    }

    // =========================================================================
    // 5. THỰC THI LỆNH ĐỒNG THỜI (ALL DEVICES)
    // =========================================================================
    if (isAll) {
        bool turnOn = (action == "ALL_ON" || action == "TURN_ON" || action == "ON" || (hasPowerParam && powerValue));
        setLivingLightPower(turnOn, commandId, sourceNodeId);
        setGarageLightPower(turnOn, commandId, sourceNodeId);
        setFanPower(turnOn, commandId, sourceNodeId);
        setPumpPower(turnOn, commandId, sourceNodeId);
        return;
    }

    // =========================================================================
    // 6. THỰC THI LỆNH CHO ĐÈN PHÒNG KHÁCH (LIVING ROOM LIGHT - b016fe3d...)
    // =========================================================================
    if (isLivingLight) {
        if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT" || action == "LIGHT_ON" || action == "LIVING_ON") {
            setLivingLightPower(true, commandId, sourceNodeId);
            if (hasBrightnessParam && brightnessValue > 0) {
                setLivingLightBrightness(brightnessValue, commandId, sourceNodeId);
            }
        } else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT" || action == "LIGHT_OFF" || action == "LIVING_OFF") {
            setLivingLightPower(false, commandId, sourceNodeId);
        } else if (action == "TOGGLE") {
            toggleLivingLight(commandId);
        } else if (action == "SET_POWER") {
            setLivingLightPower(hasPowerParam ? powerValue : true, commandId, sourceNodeId);
            if (hasBrightnessParam && brightnessValue > 0) {
                setLivingLightBrightness(brightnessValue, commandId, sourceNodeId);
            }
        } else if (action == "SET_BRIGHTNESS" || action == "BRIGHTNESS" || action == "DIM") {
            if (hasBrightnessParam) setLivingLightBrightness(brightnessValue, commandId, sourceNodeId);
        } else if (action == "INCREASE_BRIGHTNESS" || action == "BRI_UP" || action == "BRIGHTNESS_UP" || action == "TANG") {
            stepLivingLightBrightness(stepAmount, commandId, sourceNodeId);
        } else if (action == "DECREASE_BRIGHTNESS" || action == "BRI_DOWN" || action == "BRIGHTNESS_DOWN" || action == "GIAM") {
            stepLivingLightBrightness(-stepAmount, commandId, sourceNodeId);
        } else if (action == "GET_STATE" || action == "STATUS") {
            publishLivingLightState(commandId, sourceNodeId);
            publishLivingLightTelemetry();
        } else if (hasBrightnessParam) {
            setLivingLightBrightness(brightnessValue, commandId, sourceNodeId);
        } else if (hasPowerParam) {
            setLivingLightPower(powerValue, commandId, sourceNodeId);
        } else {
            Serial.printf("[LIVING LIGHT] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    // =========================================================================
    // 7. THỰC THI LỆNH CHO ĐÈN GARAGE (GARAGE LIGHT - f23a13d8...)
    // =========================================================================
    if (isGarageLight) {
        if (action == "ENABLE_AUTO_LIGHT" || action == "AUTO_LIGHT_ON") {
            autoGarageLightEnabled = true;
            saveDeviceStatesToNVS();
            Serial.println("[AUTO LIGHT] Da BAT tu dong bat den garage theo PIR.");
            publishGarageLightState(commandId, sourceNodeId);
            return;
        } else if (action == "DISABLE_AUTO_LIGHT" || action == "AUTO_LIGHT_OFF") {
            autoGarageLightEnabled = false;
            saveDeviceStatesToNVS();
            Serial.println("[AUTO LIGHT] Da TAT tu dong bat den garage theo PIR.");
            publishGarageLightState(commandId, sourceNodeId);
            return;
        }

        if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT" || action == "GARAGE_ON") {
            setGarageLightPower(true, commandId, sourceNodeId);
            if (hasBrightnessParam && brightnessValue > 0) {
                setGarageLightBrightness(brightnessValue, commandId, sourceNodeId);
            }
        } else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT" || action == "GARAGE_OFF") {
            setGarageLightPower(false, commandId, sourceNodeId);
        } else if (action == "TOGGLE") {
            toggleGarageLight(commandId);
        } else if (action == "SET_POWER") {
            setGarageLightPower(hasPowerParam ? powerValue : true, commandId, sourceNodeId);
            if (hasBrightnessParam && brightnessValue > 0) {
                setGarageLightBrightness(brightnessValue, commandId, sourceNodeId);
            }
        } else if (action == "SET_BRIGHTNESS" || action == "BRIGHTNESS" || action == "DIM") {
            if (hasBrightnessParam) setGarageLightBrightness(brightnessValue, commandId, sourceNodeId);
        } else if (action == "INCREASE_BRIGHTNESS" || action == "BRI_UP" || action == "TANG") {
            stepGarageLightBrightness(stepAmount, commandId, sourceNodeId);
        } else if (action == "DECREASE_BRIGHTNESS" || action == "BRI_DOWN" || action == "GIAM") {
            stepGarageLightBrightness(-stepAmount, commandId, sourceNodeId);
        } else if (action == "GET_STATE" || action == "STATUS") {
            publishGarageLightState(commandId, sourceNodeId);
            publishGarageLightTelemetry();
        } else if (hasBrightnessParam) {
            setGarageLightBrightness(brightnessValue, commandId, sourceNodeId);
        } else if (hasPowerParam) {
            setGarageLightPower(powerValue, commandId, sourceNodeId);
        } else {
            Serial.printf("[GARAGE LIGHT] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    // =========================================================================
    // 8. THỰC THI LỆNH CHO QUẠT THÔNG MINH (SMART FAN)
    // =========================================================================
    if (isFan) {
        if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT" || action == "FAN_ON") {
            setFanPower(true, commandId, sourceNodeId);
        } else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT" || action == "FAN_OFF") {
            setFanPower(false, commandId, sourceNodeId);
        } else if (action == "TOGGLE") {
            toggleFan(commandId);
        } else if (action == "SET_POWER") {
            setFanPower(hasPowerParam ? powerValue : true, commandId, sourceNodeId);
        } else if (action == "SET_FAN") {
            String fVal = "";
            if (doc.containsKey("fan")) fVal = doc["fan"].as<String>();
            else if (!params.isNull() && params.containsKey("fan")) fVal = params["fan"].as<String>();
            fVal.toUpperCase();

            if (fVal == "OFF" || fVal == "0") setFanPower(false, commandId, sourceNodeId);
            else if (fVal == "LOW" || fVal == "1") setFanSpeed(1, commandId, sourceNodeId);
            else if (fVal == "MED" || fVal == "MEDIUM" || fVal == "2") setFanSpeed(2, commandId, sourceNodeId);
            else if (fVal == "HIGH" || fVal == "3" || fVal == "AUTO" || fVal == "ON") setFanSpeed(3, commandId, sourceNodeId);
            else setFanPower(true, commandId, sourceNodeId);
        } else if (action == "SET_SPEED" || doc.containsKey("fan_speed") || (!params.isNull() && params.containsKey("speed"))) {
            int spd = 1;
            if (doc.containsKey("fan_speed")) spd = doc["fan_speed"].as<int>();
            else if (doc.containsKey("speed")) spd = doc["speed"].as<int>();
            else if (!params.isNull() && params.containsKey("speed")) spd = params["speed"].as<int>();
            setFanSpeed((uint8_t)spd, commandId, sourceNodeId);
        } else if (action == "GET_STATE" || action == "STATUS") {
            publishFanState(commandId, sourceNodeId);
            publishFanTelemetry();
        } else if (hasPowerParam) {
            setFanPower(powerValue, commandId, sourceNodeId);
        } else {
            Serial.printf("[FAN] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    // =========================================================================
    // 9. THỰC THI LỆNH CHO MÁY BƠM NƯỚC (WATER PUMP / MOTOR)
    // =========================================================================
    if (isPump) {
        if (action == "TURN_ON" || action == "POWER_ON" || action == "ON" || action == "BAT" || action == "PUMP_ON" || action == "MOTOR_ON") {
            setPumpPower(true, commandId, sourceNodeId);
        } else if (action == "TURN_OFF" || action == "POWER_OFF" || action == "OFF" || action == "TAT" || action == "PUMP_OFF" || action == "MOTOR_OFF") {
            setPumpPower(false, commandId, sourceNodeId);
        } else if (action == "TOGGLE") {
            togglePump(commandId);
        } else if (action == "SET_POWER") {
            setPumpPower(hasPowerParam ? powerValue : true, commandId, sourceNodeId);
        } else if (action == "GET_STATE" || action == "STATUS") {
            publishPumpState(commandId, sourceNodeId);
            publishPumpTelemetry();
        } else if (hasPowerParam) {
            setPumpPower(powerValue, commandId, sourceNodeId);
        } else {
            Serial.printf("[PUMP] Action khong ro: %s\n", action.c_str());
        }
        return;
    }

    // =========================================================================
    // 10. THỰC THI LỆNH CHO CỬA CHÍNH (MAIN DOOR - SERVO SG90)
    // =========================================================================
    if (isMainDoor) {
        if (cmdSpeed >= 0) {
            setServoSpeed((uint16_t)cmdSpeed);
        }

        if (action == "FP_ENROLL") {
            int fpId = 0;
            if (doc.containsKey("fingerprintId")) fpId = doc["fingerprintId"].as<int>();
            else if (!params.isNull() && params.containsKey("fingerprintId")) fpId = params["fingerprintId"].as<int>();

            String enrollIdStr = "";
            if (doc.containsKey("enrollmentId") && !doc["enrollmentId"].isNull()) enrollIdStr = doc["enrollmentId"].as<String>();
            else if (!params.isNull() && params.containsKey("enrollmentId") && !params["enrollmentId"].isNull()) enrollIdStr = params["enrollmentId"].as<String>();

            if (fpMode == FP_MODE_ENROLL) {
                publishFingerprintEvent("ENROLL_FAILED", fpId, -1, "Cam bien dang dang ky van tay khac", "BUSY", enrollIdStr.c_str());
                publishMainDoorCommandAck(commandId, sourceNodeId, false, "BUSY", "Cam bien dang dang ky van tay khac");
            } else {
                String errStep = "";
                String errDetail = "";
                if (fpStartEnroll(fpId, enrollIdStr.c_str(), &errStep, &errDetail)) {
                    publishMainDoorCommandAck(commandId, sourceNodeId, true);
                } else {
                    publishFingerprintEvent("ENROLL_FAILED", fpId, -1, errDetail.c_str(), errStep.c_str(), enrollIdStr.c_str());
                    publishMainDoorCommandAck(commandId, sourceNodeId, false, errStep.c_str(), errDetail.c_str());
                }
            }
            return;
        } else if (action == "FP_CANCEL") {
            String enrollIdStr = "";
            if (doc.containsKey("enrollmentId") && !doc["enrollmentId"].isNull()) enrollIdStr = doc["enrollmentId"].as<String>();
            else if (!params.isNull() && params.containsKey("enrollmentId") && !params["enrollmentId"].isNull()) enrollIdStr = params["enrollmentId"].as<String>();

            if (fpMode == FP_MODE_ENROLL && (enrollIdStr.length() == 0 || fpEnrollmentId.length() == 0 || fpEnrollmentId == enrollIdStr)) {
                fpEndEnroll(false, "Nguoi dung huy", "CANCELLED");
            }
            publishMainDoorCommandAck(commandId, sourceNodeId, true);
            return;
        } else if (action == "FP_DELETE") {
            int fpId = 0;
            if (doc.containsKey("fingerprintId")) fpId = doc["fingerprintId"].as<int>();
            else if (!params.isNull() && params.containsKey("fingerprintId")) fpId = params["fingerprintId"].as<int>();

            if (!fpReady) {
                publishMainDoorCommandAck(commandId, sourceNodeId, false, "SENSOR_NOT_READY", "Cam bien van tay chua san sang");
            } else {
                String errMsg = "";
                if (fpDeleteById(fpId, &errMsg)) {
                    publishMainDoorCommandAck(commandId, sourceNodeId, true);
                } else {
                    publishMainDoorCommandAck(commandId, sourceNodeId, false, "DELETE_FAILED", errMsg.c_str());
                }
            }
            return;
        } else if (action == "FP_LIST") {
            if (!fpReady) {
                publishMainDoorCommandAck(commandId, sourceNodeId, false, "SENSOR_NOT_READY", "Cam bien van tay chua san sang");
            } else {
                publishFingerprintListEvent();
                publishMainDoorCommandAck(commandId, sourceNodeId, true);
            }
            return;
        } else if (action == "OPEN" || action == "DOOR_OPEN" || action == "MAIN_DOOR_OPEN" || action == "UNLOCK" || action == "MO" || action == "MO_CUA") {
            mainDoorOpen(commandId, sourceNodeId);
        } else if (action == "CLOSE" || action == "DOOR_CLOSE" || action == "MAIN_DOOR_CLOSE" || action == "LOCK" || action == "DONG" || action == "DONG_CUA") {
            mainDoorClose(commandId, sourceNodeId);
        } else if (action == "STOP" || action == "DOOR_STOP" || action == "DUNG") {
            targetServoAngle = currentServoAngle;
            publishMainDoorState(commandId, sourceNodeId);
        } else if (action == "SET_SPEED" || (cmdSpeed >= 0 && cmdAngle < 0 && action != "OPEN" && action != "CLOSE")) {
            publishMainDoorState(commandId, sourceNodeId);
        } else if (action == "SET_ANGLE" || action == "SET_POSITION" || cmdAngle >= 0) {
            int ang = (cmdAngle >= 0) ? cmdAngle : targetServoAngle;
            setMainDoorPosition(ang, commandId, sourceNodeId);
        } else if (action == "GET_STATE" || action == "STATUS") {
            publishMainDoorState(commandId, sourceNodeId);
            publishMainDoorTelemetry();
        } else {
            Serial.printf("[MAIN DOOR] Action khong ro: %s\n", action.c_str());
        }
        return;
    }
}

// ====================================================================================================
// 11. ĐIỀU KHIỂN ĐÈN PHÒNG KHÁCH (LIVING ROOM LIGHT - GPIO 7) - HỖ TRỢ SMOOTH FADING (FADE ON / FADE OFF)
// ====================================================================================================
void writeLivingLightPWM(uint8_t duty) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_LIVING_LIGHT, duty);
#else
    ledcWrite(PWM_LIVING_LIGHT_CH, duty);
#endif
}

void initLivingLightPWM() {
    pinMode(PIN_LIVING_LIGHT, OUTPUT);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_LIVING_LIGHT, PWM_FREQ, PWM_RESOLUTION);
#else
    ledcSetup(PWM_LIVING_LIGHT_CH, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(PIN_LIVING_LIGHT, PWM_LIVING_LIGHT_CH);
#endif
    writeLivingLightPWM(0);
}

/**
 * Tính toán duty mục tiêu dựa trên power và brightness của đèn phòng khách
 */
void updateLivingLightTargetDuty() {
    if (!livingLightPower || livingLightBrightness == 0) {
        livingLightTargetDuty = 0.0f;
    } else {
        // Map 0% -> 100% sang 0 -> 255
        livingLightTargetDuty = (float)((uint16_t)livingLightBrightness * PWM_MAX_DUTY) / 100.0f;
    }

    if (!enableLivingLightFading) {
        livingLightCurrentDuty = livingLightTargetDuty;
        writeLivingLightPWM((uint8_t)round(livingLightCurrentDuty));
    }
}

/**
 * Cập nhật hiệu ứng Smooth Fading liên tục trong loop() (Fade On / Fade Off sáng dần, tối dần)
 */
void updateLivingLightSmoothFading() {
    if (!enableLivingLightFading) return;

    if (fabs(livingLightCurrentDuty - livingLightTargetDuty) > 0.5f) {
        livingLightCurrentDuty += (livingLightTargetDuty - livingLightCurrentDuty) * LIVING_LIGHT_FADE_SPEED;
        writeLivingLightPWM((uint8_t)round(livingLightCurrentDuty));
    } else if (livingLightCurrentDuty != livingLightTargetDuty) {
        livingLightCurrentDuty = livingLightTargetDuty;
        writeLivingLightPWM((uint8_t)round(livingLightCurrentDuty));
    }
}

void applyLivingLightOutput() {
    updateLivingLightTargetDuty();
}

/**
 * Bật hoặc Tắt đèn phòng khách (kích hoạt hiệu ứng sáng dần / tối dần)
 */
void setLivingLightPower(bool power, const char* commandId, const String& replyNodeId) {
    livingLightPower = power;

    if (power) {
        // Nếu trước đó độ sáng = 0 thì khôi phục độ sáng đã nhớ hoặc 100%
        if (livingLightBrightness == 0) {
            livingLightBrightness = (livingLightLastBrightness > 0) ? livingLightLastBrightness : 100;
        }
    } else {
        // Khi tắt, lưu lại độ sáng hiện tại để khôi phục lần sau
        if (livingLightBrightness > 0) {
            livingLightLastBrightness = livingLightBrightness;
        }
    }

    updateLivingLightTargetDuty();
    saveDeviceStatesToNVS();

    Serial.printf("[LIVING LIGHT] Den phong khach: %s | Do sang: %d%% (Chân GPIO %d) [Smooth Fading: %s]\n",
                  livingLightPower ? "BAT (ON - Sáng dần)" : "TAT (OFF - Tối dần)", livingLightBrightness, PIN_LIVING_LIGHT,
                  enableLivingLightFading ? "ON" : "OFF");

    publishLivingLightState(commandId, replyNodeId);
    publishLivingLightTelemetry();
}

/**
 * Đặt độ sáng đèn phòng khách (0% - 100%) với hiệu ứng chuyển tiếp mượt mà
 */
void setLivingLightBrightness(int brightness, const char* commandId, const String& replyNodeId) {
    brightness = constrain(brightness, 0, 100);
    livingLightBrightness = (uint8_t)brightness;

    if (brightness > 0) {
        livingLightPower = true;
        livingLightLastBrightness = (uint8_t)brightness;
    } else {
        livingLightPower = false;
    }

    updateLivingLightTargetDuty();
    saveDeviceStatesToNVS();

    Serial.printf("[LIVING LIGHT] Den phong khach: Set Do sang: %d%% (%s) [Smooth Fading: %s]\n",
                  livingLightBrightness, livingLightPower ? "ON" : "OFF",
                  enableLivingLightFading ? "ON" : "OFF");

    publishLivingLightState(commandId, replyNodeId);
    publishLivingLightTelemetry();
}

/**
 * Tăng / Giảm độ sáng đèn phòng khách tương đối theo bước
 */
void stepLivingLightBrightness(int delta, const char* commandId, const String& replyNodeId) {
    int currentBri = 0;
    if (livingLightPower) {
        currentBri = (int)livingLightBrightness;
    } else {
        // Đèn đang tắt: nếu delta > 0, lấy độ sáng nhớ trước đó hoặc tối thiểu 10%
        if (delta > 0) {
            currentBri = (livingLightLastBrightness > 0) ? (int)livingLightLastBrightness : 10;
        } else {
            currentBri = 0;
        }
    }

    int newBri = constrain(currentBri + delta, 0, 100);
    setLivingLightBrightness(newBri, commandId, replyNodeId);
}

void toggleLivingLight(const char* commandId) {
    setLivingLightPower(!livingLightPower, commandId);
}

// ====================================================================================================
// 12. ĐIỀU KHIỂN ĐÈN GARAGE (GARAGE LIGHT - GPIO 10)
// ====================================================================================================
void initGarageLightPWM() {
    pinMode(PIN_GARAGE_LIGHT, OUTPUT);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_GARAGE_LIGHT, PWM_FREQ, PWM_RESOLUTION);
#else
    ledcSetup(PWM_GARAGE_LIGHT_CH, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(PIN_GARAGE_LIGHT, PWM_GARAGE_LIGHT_CH);
#endif
    applyGarageLightOutput();
}

void applyGarageLightOutput() {
    uint8_t duty = 0;
    if (garageLightPower && garageLightBrightness > 0) {
        duty = (uint8_t)(((uint16_t)garageLightBrightness * 255) / 100);
    }
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_GARAGE_LIGHT, duty);
#else
    ledcWrite(PWM_GARAGE_LIGHT_CH, duty);
#endif
}

void setGarageLightPower(bool power, const char* commandId, const String& replyNodeId, bool isAutoOff) {
    bool prevPower = garageLightPower;
    garageLightPower = power;

    if (power) {
        lastGarageMotionTime = millis();
    }

    if (!power && prevPower) {
        if (!isAutoOff) {
            garageLightTurnOffTime = millis();
            Serial.println("[GARAGE LIGHT] Den garage vua TAT (Thu cong). Bat dau dem 5 giay bo qua cam bien chuyen dong.");
        } else {
            garageLightTurnOffTime = 0; // Tắt tự động do 5s không có chuyển động -> không khóa cảm biến, nếu có chuyển động lại thì bật ngay lập tức
            Serial.println("[GARAGE LIGHT] Den garage TU DONG TAT (do 5 giay khong co chuyen dong). San sang bat lai ngay khi co nguoi!");
        }
    }

    if (power && garageLightBrightness == 0) garageLightBrightness = 100;

    applyGarageLightOutput();
    saveDeviceStatesToNVS();

    Serial.printf("[GARAGE LIGHT] Den garage: %s | Do sang: %d%% (Chân GPIO %d)\n",
                  garageLightPower ? "BAT (ON)" : "TAT (OFF)", garageLightBrightness, PIN_GARAGE_LIGHT);

    publishGarageLightState(commandId, replyNodeId);
    publishGarageLightTelemetry();
}

void setGarageLightBrightness(int brightness, const char* commandId, const String& replyNodeId) {
    brightness = constrain(brightness, 0, 100);
    garageLightBrightness = (uint8_t)brightness;

    if (brightness > 0) {
        garageLightPower = true;
        lastGarageMotionTime = millis();
    } else {
        bool prevPower = garageLightPower;
        garageLightPower = false;
        if (prevPower) {
            garageLightTurnOffTime = millis();
            Serial.println("[GARAGE LIGHT] Den garage ve 0% (TAT). Bat dau dem 5 giay bo qua cam bien chuyen dong.");
        }
    }

    applyGarageLightOutput();
    saveDeviceStatesToNVS();

    Serial.printf("[GARAGE LIGHT] Den garage: Set Do sang: %d%% (%s)\n",
                  garageLightBrightness, garageLightPower ? "ON" : "OFF");

    publishGarageLightState(commandId, replyNodeId);
    publishGarageLightTelemetry();
}

void stepGarageLightBrightness(int delta, const char* commandId, const String& replyNodeId) {
    int currentBri = garageLightPower ? (int)garageLightBrightness : 0;
    int newBri = constrain(currentBri + delta, 0, 100);
    setGarageLightBrightness(newBri, commandId, replyNodeId);
}

void toggleGarageLight(const char* commandId) {
    setGarageLightPower(!garageLightPower, commandId);
}

// ====================================================================================================
// 13. ĐIỀU KHIỂN CỬA CHÍNH (MAIN DOOR - SERVO SG90 GPIO 8)
// ====================================================================================================
void initServoDoor() {
    pinMode(PIN_SERVO_DOOR, OUTPUT);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_SERVO_DOOR, 50, 14); // 50Hz, 14-bit resolution
#else
    ledcSetup(PWM_SERVO_CHANNEL, 50, 14); // 50Hz, 14-bit resolution
    ledcAttachPin(PIN_SERVO_DOOR, PWM_SERVO_CHANNEL);
#endif
    applyServoPwm(currentServoAngle);
    Serial.printf("[SERVO SG90] Khoi tao tren GPIO %d (Goc: %d do, Trang thai: %s, Toc do: %d ms/do %s)\n",
                  PIN_SERVO_DOOR, currentServoAngle, mainDoorOpenState ? "OPEN" : "CLOSED", 
                  servoStepDelayMs, (servoStepDelayMs == 0) ? "[TUC THI]" : "");
}

void applyServoPwm(int angle) {
    angle = constrain(angle, 0, 180);
    // Map góc 0 - 180 độ sang xung 500 - 2500 microgiây (chuẩn SG90)
    long pulseUs = map(angle, 0, 180, 500, 2500);
    // Duty cycle 14-bit (0 - 16383) với tần số 50Hz (chu kỳ 20000us)
    uint32_t duty = (uint32_t)((pulseUs * 16384UL) / 20000UL);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_SERVO_DOOR, duty);
#else
    ledcWrite(PWM_SERVO_CHANNEL, duty);
#endif
}

void setServoAngle(int angle) {
    setMainDoorPosition(angle);
}

void setServoSpeed(uint16_t delayMs) {
    servoStepDelayMs = constrain(delayMs, 0, 200);
    saveDeviceStatesToNVS();

    Serial.printf("[MAIN DOOR] >>> DA CAI DAT TOC DO SERVO: %d ms/do (", servoStepDelayMs);
    if (servoStepDelayMs == 0) {
        Serial.println("Tuc thi - Max speed - Mo tuc thi)");
    } else {
        float totalTime = (90.0f * (float)servoStepDelayMs) / 1000.0f;
        Serial.printf("Khoang %.2f giay cho 90 do)\n", totalTime);
    }
}

void updateServoMotion() {
    if (currentServoAngle == targetServoAngle) return;

    // Nếu tốc độ = 0: Quay ngay lập tức tới đích (mở/đóng tức thì)
    if (servoStepDelayMs == 0) {
        currentServoAngle = targetServoAngle;
        applyServoPwm(currentServoAngle);
        Serial.printf("[MAIN DOOR] Da den vi tri: %d do (Tuc thi)\n", currentServoAngle);
        return;
    }

    // Cơ chế Soft-motion Non-blocking: quay dần từng độ sau mỗi khoảng servoStepDelayMs
    unsigned long now = millis();
    if (now - lastServoStepTime >= servoStepDelayMs) {
        lastServoStepTime = now;

        if (currentServoAngle < targetServoAngle) {
            currentServoAngle++;
        } else if (currentServoAngle > targetServoAngle) {
            currentServoAngle--;
        }

        applyServoPwm(currentServoAngle);

        if (currentServoAngle == targetServoAngle) {
            Serial.printf("[MAIN DOOR] >>> DA HOAN TAT CHUYEN DONG: Cua o vi tri %d do (%s) <<<\n",
                          currentServoAngle, mainDoorOpenState ? "OPEN" : "CLOSED");
        }
    }
}

void mainDoorOpen(const char* commandId, const String& replyNodeId) {
    mainDoorOpenState = true;
    targetServoAngle = SERVO_DOOR_ANGLE_OPEN; // 90 độ

    // Nếu tốc độ = 0: Mở tức thì ngay tại thời điểm gọi hàm
    if (servoStepDelayMs == 0) {
        currentServoAngle = targetServoAngle;
        applyServoPwm(currentServoAngle);
    }

    saveDeviceStatesToNVS();

    if (servoStepDelayMs == 0) {
        Serial.printf("[MAIN DOOR] >>> MO CUA CHINH TUC THI (Goc: 90 do, Toc do: 0 ms/do) <<<\n");
    } else {
        float estTime = (abs(targetServoAngle - currentServoAngle) * (float)servoStepDelayMs) / 1000.0f;
        Serial.printf("[MAIN DOOR] >>> DANG MO CUA CHINH (Muc tieu: 90 do, Hien tai: %d do, Toc do: %d ms/do, Uoc tinh: %.2f s) <<<\n",
                      currentServoAngle, servoStepDelayMs, estTime);
    }

    publishMainDoorState(commandId, replyNodeId);
    publishMainDoorTelemetry();
}

void mainDoorClose(const char* commandId, const String& replyNodeId) {
    mainDoorOpenState = false;
    targetServoAngle = SERVO_DOOR_ANGLE_CLOSED; // 0 độ

    // Nếu tốc độ = 0: Đóng tức thì ngay tại thời điểm gọi hàm
    if (servoStepDelayMs == 0) {
        currentServoAngle = targetServoAngle;
        applyServoPwm(currentServoAngle);
    }

    saveDeviceStatesToNVS();

    if (servoStepDelayMs == 0) {
        Serial.printf("[MAIN DOOR] >>> DONG CUA CHINH TUC THI (Goc: 0 do, Toc do: 0 ms/do) <<<\n");
    } else {
        float estTime = (abs(targetServoAngle - currentServoAngle) * (float)servoStepDelayMs) / 1000.0f;
        Serial.printf("[MAIN DOOR] >>> DANG DONG CUA CHINH (Muc tieu: 0 do, Hien tai: %d do, Toc do: %d ms/do, Uoc tinh: %.2f s) <<<\n",
                      currentServoAngle, servoStepDelayMs, estTime);
    }

    publishMainDoorState(commandId, replyNodeId);
    publishMainDoorTelemetry();
}

void setMainDoorPosition(int angle, const char* commandId, const String& replyNodeId) {
    angle = constrain(angle, 0, 180);
    targetServoAngle = angle;
    mainDoorOpenState = (angle != SERVO_DOOR_ANGLE_CLOSED);

    // Nếu tốc độ = 0: Quay tức thì ngay lập tức
    if (servoStepDelayMs == 0) {
        currentServoAngle = targetServoAngle;
        applyServoPwm(currentServoAngle);
    }

    saveDeviceStatesToNVS();

    if (servoStepDelayMs == 0) {
        Serial.printf("[MAIN DOOR] >>> QUAY TUC THI TOI GOC %d DO <<<\n", angle);
    } else {
        float estTime = (abs(targetServoAngle - currentServoAngle) * (float)servoStepDelayMs) / 1000.0f;
        Serial.printf("[MAIN DOOR] >>> DANG QUAY TOI GOC %d DO (Hien tai: %d do, Toc do: %d ms/do, Uoc tinh: %.2f s) <<<\n",
                      angle, currentServoAngle, servoStepDelayMs, estTime);
    }

    publishMainDoorState(commandId, replyNodeId);
    publishMainDoorTelemetry();
}

// ====================================================================================================
// 14. ĐIỀU KHIỂN QUẠT THÔNG MINH (SMART FAN - RELAY GPIO 19)
// ====================================================================================================
void initFan() {
    pinMode(PIN_FAN, OUTPUT);
    applyFanOutput();
    Serial.printf("[FAN] Khoi tao tren GPIO %d (Relay kích %s, Trang thai: %s, Cap toc do: %d)\n",
                  PIN_FAN, (FAN_RELAY_ON == LOW) ? "LOW" : "HIGH", fanPower ? "BAT" : "TAT", fanSpeed);
}

void applyFanOutput() {
    digitalWrite(PIN_FAN, fanPower ? FAN_RELAY_ON : FAN_RELAY_OFF);
}

void setFanPower(bool power, const char* commandId, const String& replyNodeId) {
    fanPower = power;
    applyFanOutput();
    saveDeviceStatesToNVS();

    Serial.printf("[FAN] Quat (GPIO %d): %s (Cap toc do: %d)\n",
                  PIN_FAN, fanPower ? "BAT (ON)" : "TAT (OFF)", fanSpeed);

    publishFanState(commandId, replyNodeId);
    publishFanTelemetry();
}

void toggleFan(const char* commandId) {
    setFanPower(!fanPower, commandId);
}

void setFanSpeed(uint8_t speed, const char* commandId, const String& replyNodeId) {
    fanSpeed = constrain(speed, 0, 3);
    if (fanSpeed > 0) {
        fanPower = true;
    } else {
        fanPower = false;
    }
    applyFanOutput();
    saveDeviceStatesToNVS();

    Serial.printf("[FAN] Quat: Cai dat toc do cap %d (%s)\n",
                  fanSpeed, fanPower ? "BAT" : "TAT");

    publishFanState(commandId, replyNodeId);
    publishFanTelemetry();
}

// ====================================================================================================
// 15. ĐIỀU KHIỂN MÁY BƠM NƯỚC (WATER PUMP / MOTOR - RELAY GPIO 18)
// ====================================================================================================
void initPump() {
    pinMode(PIN_PUMP, OUTPUT);
    applyPumpOutput();
    Serial.printf("[PUMP] Khoi tao tren GPIO %d (Relay kích %s, Trang thai: %s)\n",
                  PIN_PUMP, (PUMP_RELAY_ON == LOW) ? "LOW" : "HIGH", pumpPower ? "BAT" : "TAT");
}

void applyPumpOutput() {
    digitalWrite(PIN_PUMP, pumpPower ? PUMP_RELAY_ON : PUMP_RELAY_OFF);
}

void setPumpPower(bool power, const char* commandId, const String& replyNodeId) {
    pumpPower = power;
    applyPumpOutput();
    saveDeviceStatesToNVS();

    Serial.printf("[PUMP] May bom nuoc (GPIO %d): %s\n",
                  PIN_PUMP, pumpPower ? "BAT (ON)" : "TAT (OFF)");

    publishPumpState(commandId, replyNodeId);
    publishPumpTelemetry();
}

void togglePump(const char* commandId) {
    setPumpPower(!pumpPower, commandId);
}

// ====================================================================================================
// 16. CẢM BIẾN MÔI TRƯỜNG & TỰ ĐỘNG BẬT ĐÈN GARAGE KHI CÓ CHUYỂN ĐỘNG (PIR AUTO LIGHT)
// ====================================================================================================
bool isDark() {
    int lightVal = digitalRead(PIN_LDR);
    return (lightVal == LDR_DARK_LEVEL);
}

void readAndPublishLDR() {
    int lightValue = digitalRead(PIN_LDR);
    bool dark = isDark();
    Serial.printf("[LDR] Anh sang (GPIO %d): %d (%s) -> %s\n",
                  PIN_LDR, lightValue,
                  dark ? "TROI TOI" : "TROI SANG",
                  dark ? "Kich hoat PIR bat den garage" : "Ban ngay (Khoa bat den garage theo chuyen dong)");
    publishSensorMetric(TOPIC_LDR_TELEMETRY, LDR_DEVICE_ID, "LIGHT_SENSOR", lightValue, "boolean");
}

void readAndPublishPIR() {
    int motionValue = digitalRead(PIN_PIR);
    publishSensorMetric(TOPIC_PIR_TELEMETRY, PIR_DEVICE_ID, "MOTION_SENSOR", motionValue, "boolean");
}

void readAndPublishDHT() {
    lastDHTTime = millis();
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("[DHT22] Loi doc cam bien nhiet am DHT22!");
        return;
    }

    Serial.printf("[DHT22] Nhiet do: %.1f C | Do am: %.1f %% (Chu ky gui: 30 phut)\n", temperature, humidity);
    publishSensorMetric(TOPIC_DHT_TELEMETRY, DHT_DEVICE_ID, "temperature", temperature, "C");
    publishSensorMetric(TOPIC_DHT_TELEMETRY, DHT_DEVICE_ID, "humidity", humidity, "%");
}

bool isFlameDetected() {
    return (digitalRead(PIN_FLAME) == FLAME_DETECTED_LEVEL);
}

bool isSmokeDetected() {
    return (digitalRead(PIN_SMOKE) == SMOKE_DETECTED_LEVEL);
}

void readAndPublishFlame() {
    bool flame = isFlameDetected();
    int flameVal = flame ? 1 : 0;
    Serial.printf("[FLAME] Cam bien lua (GPIO %d): %d (%s) -> %s\n",
                  PIN_FLAME, flameVal,
                  (digitalRead(PIN_FLAME) == LOW) ? "DO=LOW" : "DO=HIGH",
                  flame ? ">>> CO LUA / BAO CHAY! <<<" : "AN TOAN (Binh thuong)");
    publishSensorMetric(TOPIC_FLAME_TELEMETRY, FLAME_DEVICE_ID, "FLAME_SENSOR", flameVal, "boolean");
}

void readAndPublishSmoke() {
    bool smoke = isSmokeDetected();
    int smokeVal = smoke ? 1 : 0;
    Serial.printf("[SMOKE] Cam bien khoi MH MQ (GPIO %d): %d (%s) -> %s\n",
                  PIN_SMOKE, smokeVal,
                  (digitalRead(PIN_SMOKE) == LOW) ? "DO=LOW" : "DO=HIGH",
                  smoke ? ">>> CO KHOI / KHI GAS! <<<" : "AN TOAN (Binh thuong)");
    publishSensorMetric(TOPIC_SMOKE_TELEMETRY, SMOKE_DEVICE_ID, "SMOKE_SENSOR", smokeVal, "boolean");
}

/**
 * Kiểm tra cảnh báo Lửa và Khói tức thì (Edge Detection)
 * Khi trạng thái thay đổi -> Lập tức cảnh báo Serial và gửi Telemetry ngay!
 */
void checkFlameAndSmokeAlert() {
    int currentFlame = isFlameDetected() ? 1 : 0;
    int currentSmoke = isSmokeDetected() ? 1 : 0;

    // 1. CẢNH BÁO LỬA TỨC THÌ
    if (currentFlame != lastFlameState) {
        lastFlameState = currentFlame;
        if (currentFlame == 1) {
            Serial.println();
            Serial.println("************************************************************");
            Serial.println("  [CANH BAO KHAN CAP] >>> PHAT HIEN CO LUA / CHAY! <<<      ");
            Serial.printf ("  Thiet bi: FLAME_SENSOR (ID: %s | GPIO %d)\n", FLAME_DEVICE_ID, PIN_FLAME);
            Serial.println("************************************************************");
        } else {
            Serial.println("[FLAME] Da het lua - Tro ve trang thai AN TOAN.");
        }
        publishSensorMetric(TOPIC_FLAME_TELEMETRY, FLAME_DEVICE_ID, "FLAME_SENSOR", currentFlame, "boolean");
    }

    // 2. CẢNH BÁO KHÓI TỨC THÌ
    if (currentSmoke != lastSmokeState) {
        lastSmokeState = currentSmoke;
        if (currentSmoke == 1) {
            Serial.println();
            Serial.println("************************************************************");
            Serial.println("  [CANH BAO KHAN CAP] >>> PHAT HIEN CO KHOI / KHI GAS! <<<  ");
            Serial.printf ("  Thiet bi: SMOKE_SENSOR (ID: %s | GPIO %d)\n", SMOKE_DEVICE_ID, PIN_SMOKE);
            Serial.println("************************************************************");
        } else {
            Serial.println("[SMOKE] Khoi da tan - Tro ve trang thai AN TOAN.");
        }
        publishSensorMetric(TOPIC_SMOKE_TELEMETRY, SMOKE_DEVICE_ID, "SMOKE_SENSOR", currentSmoke, "boolean");
    }
}

/**
 * Tự động bật/tắt ĐÈN GARAGE (f23a13d8-1498-499d-aac2-04cee145f020) theo chuyển động PIR:
 *  - Khi trời tối (LDR) & có chuyển động -> TỰ ĐỘNG BẬT ĐÈN GARAGE.
 *  - Khi đèn garage đang sáng mà sau 5 giây không có chuyển động (PIR LOW liên tục 5s) -> TỰ ĐỘNG TẮT ĐÈN GARAGE.
 *  - ĐÈN PHÒNG KHÁCH (b016fe3d-36fc-4d3e-a8bd-e287d7138395) HOÀN TOÀN KHÔNG BỊ ẢNH HƯỞNG.
 */
void checkMotionAndControlLight() {
    int currentPIRState = digitalRead(PIN_PIR);
    unsigned long now = millis();

    // 1. Kiểm tra trạng thái PIR & Phát hiện chuyển động
    if (currentPIRState == HIGH) {
        // Có chuyển động -> liên tục làm mới mốc thời gian chuyển động gần nhất
        lastGarageMotionTime = now;

        // Cạnh lên: Bắt đầu phát hiện chuyển động (LOW -> HIGH)
        if (lastPIRState == LOW) {
            Serial.println();
            Serial.println("[PIR] >>> PHAT HIEN CO CHUYEN DONG (MOTION DETECTED) <<<");

            // Gửi Telemetry ngay lập tức lên MQTT để Dashboard/App cập nhật tức thì
            publishSensorMetric(TOPIC_PIR_TELEMETRY, PIR_DEVICE_ID, "MOTION_SENSOR", 1, "boolean");

            if (!autoGarageLightEnabled) {
                Serial.println("[PIR AUTO LIGHT] Tinh nang tu dong bat den garage dang TAM TAT (Disabled).");
                lastPIRState = currentPIRState;
                return;
            }

            bool dark = isDark();

            // 1.1 Kiểm tra cảm biến ánh sáng LDR: Chỉ trời tối mới kích hoạt chuyển động để bật đèn garage
            if (!dark) {
                int lightVal = digitalRead(PIN_LDR);
                Serial.printf("[PIR AUTO LIGHT] Troi dang SANG (LDR=%d) -> KHONG KICH HOAT BAT DEN GARAGE (Chi bat khi troi toi).\n", lightVal);
            }
            // 1.2 Nếu đèn garage đang bật thì không cần bật lại (thời gian chuyển động đã được cập nhật ở trên)
            else if (garageLightPower) {
                Serial.println("[PIR AUTO LIGHT] Troi toi nhung den garage DANG BAT -> Duy tri sang & tiep tuc dem gio khi het chuyen dong.");
            }
            // 1.3 Nếu đèn garage vừa tắt thủ công trong vòng 5 giây: Không bật đèn (tránh bật khi đi ra ngoài)
            else if (garageLightTurnOffTime > 0 && (now - garageLightTurnOffTime < GARAGE_LIGHT_OFF_IGNORE_WINDOW_MS)) {
                unsigned long elapsed = now - garageLightTurnOffTime;
                Serial.printf("[PIR AUTO LIGHT] Den garage vua tat thu cong cach day %lu ms (< %lu ms, di ra ngoai) -> KHONG BAT DEN.\n",
                              elapsed, GARAGE_LIGHT_OFF_IGNORE_WINDOW_MS);
            }
            // 1.4 Thỏa mãn: Trời tối (LDR) + Đèn garage đang tắt + Không bị khóa do tắt thủ công
            //     -> CHỈ BẬT ĐÈN GARAGE (GPIO 10), ĐÈN PHÒNG KHÁCH (GPIO 7) GIỮ NGUYÊN!
            else {
                Serial.println("[PIR AUTO LIGHT] Troi toi & Phat hien chuyen dong -> TU DONG BAT ĐÈN GARAGE (" GARAGE_LIGHT_DEVICE_ID ")!");
                Serial.println("[PIR AUTO LIGHT] (Ghi chu: Den phong khach b016fe3d giu nguyen, khong bi anh huong)");
                setGarageLightPower(true);
            }
        }
    } else {
        // Cạnh xuống: Hết chuyển động (HIGH -> LOW)
        if (lastPIRState == HIGH) {
            Serial.println("[PIR] Het chuyen dong (PIR LOW).");
            if (garageLightPower && autoGarageLightEnabled) {
                Serial.printf("[PIR AUTO LIGHT] Den garage dang bat -> Bat dau dem nguoc %lu ms (5s) khong chuyen dong de tu dong tat...\n",
                              GARAGE_LIGHT_AUTO_OFF_TIMEOUT_MS);
            }
            publishSensorMetric(TOPIC_PIR_TELEMETRY, PIR_DEVICE_ID, "MOTION_SENSOR", 0, "boolean");
        }
    }

    lastPIRState = currentPIRState;

    // 2. TỰ ĐỘNG TẮT ĐÈN GARAGE NẾU ĐANG SÁNG MÀ SAU 5 GIÂY KHÔNG CÓ CHUYỂN ĐỘNG
    if (autoGarageLightEnabled && garageLightPower) {
        if (currentPIRState == LOW) {
            if (now - lastGarageMotionTime >= GARAGE_LIGHT_AUTO_OFF_TIMEOUT_MS) {
                Serial.println();
                Serial.println("==================================================");
                Serial.printf("[PIR AUTO LIGHT] >>> SAU %lu MS (>= 5s) KHONG CO CHUYEN DONG -> TU DONG TAT DEN GARAGE! <<<\n",
                              now - lastGarageMotionTime);
                Serial.println("==================================================");
                setGarageLightPower(false, nullptr, "", true); // isAutoOff = true
            }
        }
    }
}

void printSensorsSerial() {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int lightVal = digitalRead(PIN_LDR);
    int pirVal = digitalRead(PIN_PIR);
    bool dark = isDark();

    Serial.println();
    Serial.println("==================================================");
    Serial.println("         THONG SO CAM BIEN (SENSORS STATUS)       ");
    Serial.println("==================================================");
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("  [DHT22]  Nhiet do / Do am : LOI DOC CAM BIEN DHT22");
    } else {
        Serial.printf("  [DHT22]  Nhiet do         : %.1f C\n", temperature);
        Serial.printf("  [DHT22]  Do am            : %.1f %%\n", humidity);
    }
    Serial.printf("  [LDR]    Anh sang (GPIO %d): %d (%s -> %s)\n",
                  PIN_LDR, lightVal,
                  dark ? "TROI TOI" : "TROI SANG",
                  dark ? "KICH HOAT PIR BAT DEN GARAGE" : "KHOA PIR BAT DEN (Khong bat den ban ngay)");
    Serial.printf("  [PIR]    Chuyen dong (GPIO %d): %d (%s)\n",
                  PIN_PIR, pirVal, (pirVal == HIGH) ? "CO CHUYEN DONG" : "KHONG CO CHUYEN DONG");
    bool flame = isFlameDetected();
    bool smoke = isSmokeDetected();
    Serial.printf("  [FLAME]  Lua (GPIO %d)       : %d (%s)\n",
                  PIN_FLAME, flame ? 1 : 0, flame ? ">>> CO LUA / BAO CHAY! <<<" : "AN TOAN (Khong co lua)");
    Serial.printf("  [SMOKE]  Khoi / Gas (GPIO %d): %d (%s)\n",
                  PIN_SMOKE, smoke ? 1 : 0, smoke ? ">>> CO KHOI / GAS! <<<" : "AN TOAN (Khong co khoi)");
    Serial.println("==================================================\n");
}

void initHardwarePins() {
    // 1. Cảm biến môi trường & An toàn PCCC
    pinMode(PIN_LDR, INPUT);
    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_FLAME, INPUT_PULLUP);
    pinMode(PIN_SMOKE, INPUT_PULLUP);
    dht.begin();

    // 2. Đèn phòng khách (Living Room Light - GPIO 7)
    initLivingLightPWM();

    // 3. Đèn garage (Garage Light - GPIO 10)
    initGarageLightPWM();

    // 4. Cửa chính (Main Door - Servo SG90 GPIO 8)
    initServoDoor();

    // 5. Quạt thông minh (Smart Fan - Relay GPIO 19)
    initFan();

    // 6. Máy bơm nước (Water Pump / Motor - Relay GPIO 18)
    initPump();
}

// ====================================================================================================
// 16B. CẢM BIẾN VÂN TAY AS608 - ĐĂNG KÝ & MỞ KHÓA CỬA CHÍNH (70de950a-14b3-4b44-ac67-e2ff4e2c5d17)
// ====================================================================================================
// - Chế độ QUÉT (mặc định): đặt ngón tay -> khớp vân tay -> mainDoorOpen() -> tự đóng sau FP_DOOR_AUTO_CLOSE_MS.
// - Chế độ ĐĂNG KÝ: 'fp enroll [id]' -> đặt tay lần 1 -> nhấc tay -> đặt lại lần 2 -> lưu vào ID.
// - Toàn bộ chạy non-blocking trong loop() (state machine), không làm đứng Servo / cảm biến / MQTT.
// - Sự kiện được publish lên topic .../devices/<MAIN_DOOR>/event khi đang online (chuẩn bị cho Backend/App).
HardwareSerial& fpSerial = Serial1;
Adafruit_Fingerprint finger(&Serial1);
// (enum FpMode / FpEnrollStep và fpReady / fpMode / fpEnrollmentId đã khai báo ở mục 5)

uint16_t      fpCapacity           = 162;                 // AS608 thường 162 mẫu (đọc lại từ module)
uint32_t      fpBaud               = 57600;
unsigned long fpStepStartedAt      = 0;
unsigned long fpLastPoll           = 0;
bool          fpWaitRelease        = false;  // Đã xử lý ngón tay hiện tại, chờ nhấc tay ra mới quét tiếp
uint8_t       fpFailedAttempts     = 0;
uint8_t       fpCommErrors         = 0;
bool          fpLockedOut          = false;
unsigned long fpLockoutStart       = 0;
bool          fpAutoClosePending   = false;
unsigned long fpDoorOpenedAt       = 0;
uint32_t      fpAutoCloseMs        = FP_DOOR_AUTO_CLOSE_MS;

const char* fpCodeText(uint8_t code) {
    switch (code) {
        case FINGERPRINT_OK:               return "OK";
        case FINGERPRINT_PACKETRECIEVEERR: return "Loi nhan goi UART";
        case FINGERPRINT_NOFINGER:         return "Khong co ngon tay";
        case FINGERPRINT_IMAGEFAIL:        return "Chup anh that bai";
        case FINGERPRINT_IMAGEMESS:        return "Anh qua mo/ban";
        case FINGERPRINT_FEATUREFAIL:      return "Khong trich xuat duoc dac trung";
        case FINGERPRINT_INVALIDIMAGE:     return "Anh khong hop le";
        case FINGERPRINT_NOTFOUND:         return "Khong tim thay trong CSDL";
        case FINGERPRINT_ENROLLMISMATCH:   return "2 lan quet khong khop";
        case FINGERPRINT_BADLOCATION:      return "ID vuot qua dung luong";
        case FINGERPRINT_FLASHERR:         return "Loi ghi Flash";
        case FINGERPRINT_DELETEFAIL:       return "Xoa mau that bai";
        case FINGERPRINT_DBCLEARFAIL:      return "Xoa CSDL that bai";
        case FINGERPRINT_TIMEOUT:          return "Timeout (khong phan hoi)";
        case FINGERPRINT_BADPACKET:        return "Goi tin loi";
        default:                           return "Loi khong xac dinh";
    }
}

bool fpIsCommError(uint8_t code) {
    return code == FINGERPRINT_PACKETRECIEVEERR || code == FINGERPRINT_TIMEOUT || code == FINGERPRINT_BADPACKET;
}

void publishFingerprintEvent(const char* event, int fingerId, int confidence, const char* detail, const char* step, const char* enrollmentId) {
    if (offlineTestMode || !mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = MAIN_DOOR_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_DOOR;
    doc["source"]     = "FINGERPRINT";
    doc["event"]      = event;
    if (fingerId >= 0) doc["fingerprintId"] = fingerId;
    if (confidence >= 0) doc["confidence"] = confidence;
    if (detail && strlen(detail) > 0) doc["detail"] = detail;
    if (step && strlen(step) > 0) doc["step"] = step;
    if (enrollmentId && strlen(enrollmentId) > 0) doc["enrollmentId"] = enrollmentId;
    doc["doorState"]  = mainDoorOpenState ? "OPEN" : "CLOSED";
    doc["uptime"]     = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_MAIN_DOOR_EVENT.c_str(), buf, false);
}

void publishFingerprintListEvent() {
    if (!fpReady || offlineTestMode || !mqttClient.connected()) return;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    doc["nodeId"]     = NODE_ID;
    doc["deviceId"]   = MAIN_DOOR_DEVICE_ID;
    doc["deviceType"] = DEVICE_TYPE_DOOR;
    doc["source"]     = "FINGERPRINT";
    doc["event"]      = "LIST";
    JsonArray ids = doc["ids"].to<JsonArray>();
    for (uint16_t id = 1; id < fpCapacity; id++) {
        if (fpIdExists(id)) {
            ids.add(id);
        }
    }
    doc["uptime"] = millis() / 1000;

    char buf[2048];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(TOPIC_MAIN_DOOR_EVENT.c_str(), buf, false);
    Serial.printf("[FINGERPRINT] Da gui danh sach %u van tay len Backend.\n", (unsigned int)ids.size());
}

bool initFingerprint() {
#if ENABLE_FINGERPRINT
#if PIN_FP_TOUCH >= 0
    pinMode(PIN_FP_TOUCH, INPUT);
#endif
    fpReady = false;
    fpCommErrors = 0;
    const uint32_t bauds[] = { 57600, 115200, 9600 }; // 57600 là mặc định của AS608
    for (uint32_t b : bauds) {
        fpSerial.begin(b, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
        delay(100);
        if (finger.verifyPassword()) {
            fpBaud = b;
            fpReady = true;
            break;
        }
    }

    if (!fpReady) {
        Serial.printf("[FINGERPRINT] !!! KHONG TIM THAY AS608 (RX=GPIO%d, TX=GPIO%d) !!!\n", PIN_FP_RX, PIN_FP_TX);
        Serial.println("[FINGERPRINT]  -> Kiem tra: TX cam bien -> GPIO16, RX cam bien -> GPIO17 (dao cheo), V+ -> 3V3, GND chung.");
        Serial.println("[FINGERPRINT]  -> Sua xong go 'fp init' de ket noi lai.");
        return false;
    }

    if (finger.getParameters() == FINGERPRINT_OK && finger.capacity > 0) {
        fpCapacity = finger.capacity;
    }
    finger.getTemplateCount();
    Serial.printf("[FINGERPRINT] AS608 SAN SANG | Baud: %lu | Dung luong: %u mau | Da dang ky: %u | Security: %u\n",
                  (unsigned long)fpBaud, fpCapacity, finger.templateCount, finger.security_level);
    if (finger.templateCount == 0) {
        Serial.println("[FINGERPRINT] Chua co van tay nao. Go 'fp enroll' de dang ky van tay dau tien.");
    }
    return true;
#else
    return false;
#endif
}

/**
 * Đọc bảng chỉ mục (ReadIndexTable 0x1F) -> bitmap 256 ID / trang. Nhanh hơn nhiều so với loadModel từng ID.
 */
bool fpReadIndexPage(uint8_t page, uint8_t out[32]) {
    uint8_t data[2] = { 0x1F, page };
    Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data), data);
    finger.writeStructuredPacket(packet);
    if (finger.getStructuredPacket(&packet) != FINGERPRINT_OK) return false;
    if (packet.type != FINGERPRINT_ACKPACKET || packet.data[0] != FINGERPRINT_OK) return false;
    memcpy(out, &packet.data[1], 32);
    return true;
}

bool fpIdExists(uint16_t id) {
    uint8_t table[32];
    if (fpReadIndexPage(id / 256, table)) {
        uint16_t bit = id % 256;
        return (table[bit / 8] >> (bit % 8)) & 0x01;
    }
    return finger.loadModel(id) == FINGERPRINT_OK; // Fallback nếu module không hỗ trợ 0x1F
}

int fpFindFreeId() {
    for (uint16_t id = 1; id < fpCapacity; id++) {
        if (!fpIdExists(id)) return id;
    }
    return -1;
}

void fpListTemplates() {
    if (!fpReady) { Serial.println("[FINGERPRINT] Cam bien chua san sang. Go 'fp init'."); return; }
    finger.getTemplateCount();
    Serial.printf("[FINGERPRINT] Da dang ky %u / %u van tay. Danh sach ID:", finger.templateCount, fpCapacity);
    uint16_t printed = 0;
    for (uint16_t id = 1; id < fpCapacity; id++) {
        if (fpIdExists(id)) {
            Serial.printf("%s#%u", (printed % 15 == 0) ? "\n  " : ", ", id);
            printed++;
        }
    }
    Serial.println(printed == 0 ? " (trong)" : "");
}

bool fpDeleteById(int id, String* errMsg) {
    if (id <= 0 || id >= fpCapacity) {
        if (errMsg) *errMsg = "ID khong hop le (1 - " + String(fpCapacity - 1) + ")";
        return false;
    }
    uint8_t p = finger.deleteModel(id);
    if (p == FINGERPRINT_OK) {
        Serial.printf("[FINGERPRINT] Da XOA van tay ID #%d.\n", id);
        publishFingerprintEvent("DELETED", id, -1);
        return true;
    }
    if (errMsg) *errMsg = fpCodeText(p);
    Serial.printf("[FINGERPRINT] Xoa ID #%d that bai: %s\n", id, fpCodeText(p));
    return false;
}

void fpEndEnroll(bool success, const char* reason, const char* step) {
    const char* enrollPtr = fpEnrollmentId.length() > 0 ? fpEnrollmentId.c_str() : nullptr;
    if (success) {
        Serial.printf("[FINGERPRINT] >>> DANG KY THANH CONG VAN TAY ID #%u <<<\n", fpEnrollId);
        publishFingerprintEvent("ENROLL_SUCCESS", fpEnrollId, -1, nullptr, step ? step : "DONE", enrollPtr);
    } else {
        Serial.printf("[FINGERPRINT] !!! DANG KY ID #%u THAT BAI: %s !!!\n", fpEnrollId, reason ? reason : "");
        publishFingerprintEvent("ENROLL_FAILED", fpEnrollId, -1, reason, step, enrollPtr);
    }
    fpEnrollmentId = "";
    fpMode = FP_MODE_SCAN;
    fpWaitRelease = true; // Tránh ngón tay vẫn đặt trên cảm biến bị quét mở cửa ngay
}

bool fpStartEnroll(int id, const char* enrollmentId, String* errStep, String* errDetail) {
    if (!fpReady) {
        Serial.println("[FINGERPRINT] Cam bien chua san sang. Go 'fp init'.");
        if (errStep) *errStep = "SENSOR_NOT_READY";
        if (errDetail) *errDetail = "Cam bien van tay chua san sang";
        return false;
    }
    if (id <= 0) {
        id = fpFindFreeId();
        if (id < 0) {
            Serial.println("[FINGERPRINT] Bo nho cam bien da day!");
            if (errStep) *errStep = "MEMORY_FULL";
            if (errDetail) *errDetail = "Bo nho cam bien da day";
            return false;
        }
    }
    if (id >= fpCapacity) {
        Serial.printf("[FINGERPRINT] ID khong hop le. Chon ID tu 1 den %u.\n", fpCapacity - 1);
        if (errStep) *errStep = "INVALID_SLOT";
        if (errDetail) *errDetail = "ID vuot qua dung luong cam bien";
        return false;
    }
    if (fpIdExists(id)) {
        Serial.printf("[FINGERPRINT] ID #%d da co van tay. Xoa truoc bang 'fp delete %d' hoac chon ID khac.\n", id, id);
        if (errStep) *errStep = "SLOT_OCCUPIED";
        if (errDetail) *errDetail = "Vi tri #" + String(id) + " da co van tay trong cam bien";
        return false;
    }

    fpEnrollId = id;
    fpEnrollmentId = (enrollmentId && strlen(enrollmentId) > 0) ? String(enrollmentId) : "";
    fpMode = FP_MODE_ENROLL;
    fpEnrollStep = FP_EN_WAIT_FINGER_1;
    fpStepStartedAt = millis();
    Serial.printf("\n[FINGERPRINT] === DANG KY VAN TAY ID #%u ===\n", fpEnrollId);
    Serial.println("[FINGERPRINT] (1/2) Dat ngon tay len cam bien... (go 'fp cancel' de huy)");
    publishFingerprintEvent("ENROLL_PROGRESS", fpEnrollId, -1, nullptr, "PLACE_FINGER_1",
                            fpEnrollmentId.length() > 0 ? fpEnrollmentId.c_str() : nullptr);
    return true;
}

void fpEnrollGoRemove(FpEnrollStep next, bool isRetry) {
    fpEnrollStep = FP_EN_WAIT_REMOVE;
    fpEnrollAfterRemove = next;
    fpStepStartedAt = millis();
    Serial.println("[FINGERPRINT] Nhac ngon tay ra khoi cam bien...");
    publishFingerprintEvent("ENROLL_PROGRESS", fpEnrollId, -1, nullptr,
                            isRetry ? "RETRY_REMOVE_FINGER" : "REMOVE_FINGER",
                            fpEnrollmentId.length() > 0 ? fpEnrollmentId.c_str() : nullptr);
}

void fpProcessEnroll() {
    unsigned long now = millis();
    if (now - fpStepStartedAt > FP_ENROLL_STEP_TIMEOUT_MS) {
        fpEndEnroll(false, "Het thoi gian cho (timeout)", "TIMEOUT");
        return;
    }

    uint8_t p = finger.getImage();
    if (fpIsCommError(p)) { fpCommErrors++; return; }
    fpCommErrors = 0;

    switch (fpEnrollStep) {
        case FP_EN_WAIT_REMOVE:
            if (p == FINGERPRINT_NOFINGER) {
                fpEnrollStep = fpEnrollAfterRemove;
                fpStepStartedAt = now;
                if (fpEnrollStep == FP_EN_WAIT_FINGER_2) {
                    Serial.println("[FINGERPRINT] (2/2) Dat LAI CUNG ngon tay do len cam bien...");
                    publishFingerprintEvent("ENROLL_PROGRESS", fpEnrollId, -1, nullptr, "PLACE_FINGER_2",
                                            fpEnrollmentId.length() > 0 ? fpEnrollmentId.c_str() : nullptr);
                } else {
                    Serial.println("[FINGERPRINT] (1/2) Dat ngon tay len cam bien...");
                    publishFingerprintEvent("ENROLL_PROGRESS", fpEnrollId, -1, nullptr, "PLACE_FINGER_1",
                                            fpEnrollmentId.length() > 0 ? fpEnrollmentId.c_str() : nullptr);
                }
            }
            break;

        case FP_EN_WAIT_FINGER_1:
            if (p != FINGERPRINT_OK) return; // Chưa có tay / ảnh lỗi -> chờ tiếp
            p = finger.image2Tz(1);
            if (p != FINGERPRINT_OK) {
                Serial.printf("[FINGERPRINT] Anh chua ro (%s). Thu lai.\n", fpCodeText(p));
                fpEnrollGoRemove(FP_EN_WAIT_FINGER_1, true);
                return;
            }
            // Kiểm tra trùng: vân tay này đã đăng ký ở ID khác chưa?
            if (finger.fingerFastSearch() == FINGERPRINT_OK) {
                Serial.printf("[FINGERPRINT] Van tay nay DA DUOC DANG KY voi ID #%u (do tin cay %u).\n",
                              finger.fingerID, finger.confidence);
                String dupDetail = "Van tay da ton tai (#" + String(finger.fingerID) + ")";
                fpEndEnroll(false, dupDetail.c_str(), "DUPLICATE");
                return;
            }
            Serial.println("[FINGERPRINT] Lan 1 OK.");
            fpEnrollGoRemove(FP_EN_WAIT_FINGER_2, false);
            break;

        case FP_EN_WAIT_FINGER_2:
            if (p != FINGERPRINT_OK) return;
            p = finger.image2Tz(2);
            if (p != FINGERPRINT_OK) {
                Serial.printf("[FINGERPRINT] Anh chua ro (%s). Thu lai lan 2.\n", fpCodeText(p));
                fpEnrollGoRemove(FP_EN_WAIT_FINGER_2, true);
                return;
            }
            p = finger.createModel();
            if (p != FINGERPRINT_OK) {
                Serial.printf("[FINGERPRINT] Tao mau that bai (%s). Lam lai tu dau.\n", fpCodeText(p));
                fpEnrollGoRemove(FP_EN_WAIT_FINGER_1, true);
                return;
            }
            p = finger.storeModel(fpEnrollId);
            if (p != FINGERPRINT_OK) {
                fpEndEnroll(false, fpCodeText(p), "STORE_FAILED");
                return;
            }
            fpEndEnroll(true, nullptr, "DONE");
            break;
    }
}

void fpHandleMatch(uint16_t id, uint16_t confidence) {
    fpFailedAttempts = 0;
    // Đẩy Servo mở cửa NGAY LẬP TỨC trước khi ghi NVS / gửi MQTT / in log
    mainDoorOpenState = true;
    targetServoAngle = SERVO_DOOR_ANGLE_OPEN;
    currentServoAngle = SERVO_DOOR_ANGLE_OPEN;
    applyServoPwm(currentServoAngle);

    Serial.printf("\n[FINGERPRINT] >>> VAN TAY HOP LE: ID #%u (Do tin cay: %u) -> MO KHOA CUA CHINH TUC THI <<<\n", id, confidence);
    mainDoorOpen();
    if (fpAutoCloseMs > 0) {
        fpAutoClosePending = true;
        fpDoorOpenedAt = millis();
        Serial.printf("[FINGERPRINT] Cua se tu dong dong sau %lu giay.\n", (unsigned long)(fpAutoCloseMs / 1000));
    }
    publishFingerprintEvent("UNLOCK_SUCCESS", id, confidence);
}

void fpHandleReject(int confidence) {
    fpFailedAttempts++;
    Serial.printf("[FINGERPRINT] !!! VAN TAY KHONG HOP LE - TU CHOI MO CUA (%u/%u) !!!\n",
                  fpFailedAttempts, FP_MAX_FAILED_ATTEMPTS);
    publishFingerprintEvent("UNLOCK_FAILED", -1, confidence);

    if (fpFailedAttempts >= FP_MAX_FAILED_ATTEMPTS) {
        fpLockedOut = true;
        fpLockoutStart = millis();
        Serial.printf("[FINGERPRINT] !!! SAI QUA %u LAN - KHOA QUET VAN TAY %lu GIAY !!!\n",
                      FP_MAX_FAILED_ATTEMPTS, (unsigned long)(FP_LOCKOUT_MS / 1000));
        publishFingerprintEvent("LOCKOUT", -1, -1);
    }
}

void fpProcessScan() {
    unsigned long now = millis();
    if (fpLockedOut) {
        if (now - fpLockoutStart < FP_LOCKOUT_MS) return;
        fpLockedOut = false;
        fpFailedAttempts = 0;
        Serial.println("[FINGERPRINT] Het thoi gian khoa, co the quet van tay lai.");
    }

#if PIN_FP_TOUCH >= 0
    // Có chân WAK: chỉ giao tiếp UART khi thực sự có ngón tay chạm vào
    if (digitalRead(PIN_FP_TOUCH) != FP_TOUCH_ACTIVE_LEVEL) { fpWaitRelease = false; return; }
#endif

    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { fpWaitRelease = false; fpCommErrors = 0; return; }
    if (fpIsCommError(p)) { fpCommErrors++; return; }
    fpCommErrors = 0;
    if (fpWaitRelease) return;      // Ngón tay cũ vẫn chưa nhấc ra
    if (p != FINGERPRINT_OK) return; // Ảnh lỗi -> thử lại ngay chu kỳ sau (không khóa fpWaitRelease)

    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
        // Ngón tay vừa chạm nên ảnh chưa rõ -> KHÔNG khóa fpWaitRelease để tự quét lại ngay sau 40ms
        return;
    }

    fpWaitRelease = true; // Chỉ chờ nhấc tay sau khi đã có mẫu vân tay rõ để so khớp
    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK && finger.confidence >= FP_MIN_CONFIDENCE) {
        fpHandleMatch(finger.fingerID, finger.confidence);
    } else if (p == FINGERPRINT_OK || p == FINGERPRINT_NOTFOUND) {
        fpHandleReject(p == FINGERPRINT_OK ? finger.confidence : 0);
    } else {
        Serial.printf("[FINGERPRINT] Loi tim kiem: %s\n", fpCodeText(p));
    }
}

void updateFingerprint() {
    unsigned long now = millis();

    // Tự đóng cửa sau khi mở bằng vân tay (bỏ qua nếu cửa đã được đóng bằng cách khác)
    if (fpAutoClosePending) {
        if (!mainDoorOpenState) {
            fpAutoClosePending = false;
        } else if (now - fpDoorOpenedAt >= fpAutoCloseMs) {
            fpAutoClosePending = false;
            Serial.println("[FINGERPRINT] Het thoi gian mo -> TU DONG DONG CUA CHINH.");
            mainDoorClose();
        }
    }

    if (!fpReady) return;
    if (now - fpLastPoll < (fpMode == FP_MODE_ENROLL ? 100UL : (unsigned long)FP_SCAN_INTERVAL_MS)) return;
    fpLastPoll = now;

    if (fpMode == FP_MODE_ENROLL) {
        fpProcessEnroll();
    } else if (fpScanEnabled) {
        fpProcessScan();
    }

    // Mất kết nối liên tục -> ngừng polling để loop() không bị chậm (mỗi lần timeout ~1s)
    if (fpCommErrors >= 5) {
        fpReady = false;
        fpMode = FP_MODE_SCAN;
        Serial.println("[FINGERPRINT] !!! MAT KET NOI AS608 (loi UART lien tiep). Kiem tra day roi go 'fp init'. !!!");
    }
}

void printFingerprintHelp() {
    Serial.println("  10. Cảm biến vân tay AS608 (UART RX=GPIO16, TX=GPIO17) - Mở khóa Cửa chính:");
    Serial.println("     - Đăng ký vân tay (ID tự chọn) : 'fp enroll'          | Đăng ký vào ID cụ thể: 'fp enroll 5'");
    Serial.println("     - Hủy đăng ký đang chạy        : 'fp cancel'");
    Serial.println("     - Danh sách / Số lượng vân tay : 'fp list' / 'fp count'");
    Serial.println("     - Xóa 1 vân tay / Xóa tất cả   : 'fp delete 5' / 'fp clear yes'");
    Serial.println("     - Bật / Tắt quét mở khóa       : 'fp scan on' / 'fp scan off'");
    Serial.println("     - Thời gian tự đóng cửa (giây) : 'fp autoclose 5' (0 = không tự đóng)");
    Serial.println("     - Thông tin / Kết nối lại      : 'fp info' / 'fp init'");
}

/**
 * Xử lý lệnh Serial bắt đầu bằng "fp". Trả về true nếu đã xử lý.
 */
bool handleFingerprintCommand(const String& lowerCmd) {
    if (!(lowerCmd == "fp" || lowerCmd.startsWith("fp ") || lowerCmd.startsWith("vantay"))) return false;

    String sub = lowerCmd.startsWith("vantay") ? lowerCmd.substring(6) : lowerCmd.substring(2);
    sub.trim();

    if (sub == "" || sub == "help") {
        Serial.println();
        printFingerprintHelp();
        return true;
    }

    if (sub == "init") {
        fpMode = FP_MODE_SCAN;
        initFingerprint();
        return true;
    }

    if (sub == "info") {
        Serial.printf("[FINGERPRINT] Trang thai: %s | Che do: %s | Quet mo khoa: %s | Tu dong dong: %lus\n",
                      fpReady ? "SAN SANG" : "CHUA KET NOI",
                      fpMode == FP_MODE_ENROLL ? "DANG KY" : "QUET",
                      fpScanEnabled ? "BAT" : "TAT",
                      (unsigned long)(fpAutoCloseMs / 1000));
        if (fpReady && finger.getParameters() == FINGERPRINT_OK) {
            finger.getTemplateCount();
            Serial.printf("[FINGERPRINT] Baud: %lu | Dung luong: %u | Da dang ky: %u | Security level: %u | Addr: 0x%08lX\n",
                          (unsigned long)fpBaud, finger.capacity, finger.templateCount, finger.security_level,
                          (unsigned long)finger.device_addr);
        }
        if (fpLockedOut) {
            Serial.printf("[FINGERPRINT] Dang bi KHOA quet, con %lu giay.\n",
                          (unsigned long)((FP_LOCKOUT_MS - (millis() - fpLockoutStart)) / 1000));
        }
        return true;
    }

    if (!fpReady) {
        Serial.println("[FINGERPRINT] Cam bien chua san sang. Kiem tra day va go 'fp init'.");
        return true;
    }

    if (sub == "enroll" || sub == "add" || sub == "dang ky" || sub.startsWith("enroll ") || sub.startsWith("add ")) {
        int spaceIdx = sub.indexOf(' ');
        int id = (spaceIdx > 0) ? sub.substring(spaceIdx + 1).toInt() : 0;
        if (fpMode == FP_MODE_ENROLL) {
            Serial.println("[FINGERPRINT] Dang trong qua trinh dang ky. Go 'fp cancel' truoc.");
            return true;
        }
        fpStartEnroll(id);
        return true;
    }

    if (sub == "cancel" || sub == "huy") {
        if (fpMode == FP_MODE_ENROLL) fpEndEnroll(false, "Nguoi dung huy", "CANCELLED");
        else Serial.println("[FINGERPRINT] Khong co qua trinh dang ky nao dang chay.");
        return true;
    }

    if (sub == "list") { fpListTemplates(); return true; }

    if (sub == "count") {
        finger.getTemplateCount();
        Serial.printf("[FINGERPRINT] Da dang ky %u / %u van tay.\n", finger.templateCount, fpCapacity);
        return true;
    }

    if (sub.startsWith("delete ") || sub.startsWith("del ") || sub.startsWith("xoa ")) {
        int id = sub.substring(sub.indexOf(' ') + 1).toInt();
        String errMsg = "";
        if (!fpDeleteById(id, &errMsg) && errMsg.length() > 0) {
            Serial.printf("[FINGERPRINT] %s\n", errMsg.c_str());
        }
        return true;
    }

    if (sub == "clear") {
        Serial.println("[FINGERPRINT] Lenh nay XOA TOAN BO van tay! Go 'fp clear yes' de xac nhan.");
        return true;
    }

    if (sub == "clear yes") {
        uint8_t p = finger.emptyDatabase();
        if (p == FINGERPRINT_OK) {
            Serial.println("[FINGERPRINT] Da XOA TOAN BO van tay trong cam bien.");
            publishFingerprintEvent("CLEARED", -1, -1);
        } else {
            Serial.printf("[FINGERPRINT] Xoa toan bo that bai: %s\n", fpCodeText(p));
        }
        return true;
    }

    if (sub == "scan on" || sub == "scan off") {
        fpScanEnabled = (sub == "scan on");
        Serial.printf("[FINGERPRINT] Quet van tay mo khoa: %s\n", fpScanEnabled ? "BAT" : "TAT");
        return true;
    }

    if (sub.startsWith("autoclose ")) {
        long sec = sub.substring(10).toInt();
        if (sec < 0) sec = 0;
        fpAutoCloseMs = (uint32_t)sec * 1000UL;
        Serial.printf("[FINGERPRINT] Tu dong dong cua sau khi mo bang van tay: %s\n",
                      sec == 0 ? "TAT" : (String(sec) + " giay").c_str());
        return true;
    }

    Serial.println("[FINGERPRINT] Lenh 'fp' khong hop le. Go 'fp help'.");
    return true;
}

// ====================================================================================================
// 17. GIAO DIỆN LỆNH QUA SERIAL MONITOR (115200 BAUD)
// ====================================================================================================
void printHelp() {
    Serial.println("\n====================== HƯỚNG DẪN LỆNH SERIAL ======================");
    Serial.println("  1. Chuyển đổi chế độ hoạt động:");
    Serial.println("     - 'mode offline' / 'offline' : Chạy offline (không WiFi/MQTT, loop siêu tốc)");
    Serial.println("     - 'mode online' / 'online'   : Chạy online (kết nối WiFi, MQTT, BLE)");
    Serial.println("  2. Điều khiển Cửa chính (Servo SG90 - GPIO 8):");
    Serial.println("     - Mở cửa chính 90 độ tức thì : 'door open', 'mo cua', '8', 'open'");
    Serial.println("     - Đóng cửa chính 0 độ tức thì: 'door close', 'dong cua', '9', 'close'");
    Serial.println("     - Dừng quay                  : 'door stop', 'stop'");
    Serial.println("     - Quay góc tùy chọn (0-180)  : 'door 45', 'servo 60'");
    Serial.println("     - Cài tốc độ quay (0 = tức thì): 'speed 0', 'speed 20'");
    Serial.println("  3. Điều khiển Đèn phòng khách (GPIO 7):");
    Serial.println("     - Bật / Tắt đèn phòng khách  : 'light on' / 'light off', 'living on' / 'living off', '6' / '7'");
    Serial.println("     - Đảo trạng thái đèn phòng   : 'light toggle', 'living toggle'");
    Serial.println("     - Độ sáng đèn phòng khách    : 'bri 80', 'living bri 80', 'b 60'");
    Serial.println("     - Tăng / Giảm độ sáng        : '+', '-', '+ 20', '- 20'");
    Serial.println("     - Bật/tắt hiệu ứng Fade      : 'fade on' / 'fade off' (Sáng dần / tối dần)");
    Serial.println("  4. Điều khiển Đèn garage (GPIO 10 - Tự động bật theo PIR & tắt sau 5s):");
    Serial.println("     - Bật / Tắt đèn garage       : 'garage on' / 'garage off', 'bat den garage' / 'tat den garage'");
    Serial.println("     - Đảo trạng thái đèn garage  : 'garage toggle'");
    Serial.println("     - Độ sáng đèn garage         : 'garage bri 80', 'gbri 60'");
    Serial.println("     - Bật/tắt tự động theo PIR   : 'auto light on' / 'auto light off' (Bật khi tối & tự tắt sau 5s)");
    Serial.println("  5. Điều khiển Quạt thông minh (GPIO 19):");
    Serial.println("     - Bật / Tắt quạt             : 'fan on' / 'fan off', 'bat quat' / 'tat quat', '1' / '0'");
    Serial.println("     - Đảo trạng thái quạt        : 'fan toggle'");
    Serial.println("     - Cài tốc độ quạt (1..3)     : 'fan speed 1', 'fan speed 2', 'fan speed 3'");
    Serial.println("  6. Điều khiển Máy bơm nước (GPIO 18):");
    Serial.println("     - Bật / Tắt máy bơm          : 'pump on' / 'pump off', 'bat bom' / 'tat bom', '2' / '3'");
    Serial.println("     - Đảo trạng thái máy bơm     : 'pump toggle'");
    Serial.println("  7. Điều khiển Đồng thời:");
    Serial.println("     - Bật tất cả                 : 'all on', 'bat het', 'a'");
    Serial.println("     - Tắt tất cả                 : 'all off', 'tat het', 'b'");
    Serial.println("  8. Cảm biến môi trường & An toàn PCCC (LDR, PIR, DHT22, Lửa, Khói):");
    Serial.println("     - Đọc toàn bộ cảm biến       : 'sensors', 'dht', 'ldr', 'cam bien'");
    Serial.println("     - Đọc cảm biến lửa           : 'flame', 'lua'");
    Serial.println("     - Đọc cảm biến khói          : 'smoke', 'khoi'");
    Serial.println("  9. Tiện ích hệ thống:");
    Serial.println("     - 'status' / 'info' / 's'    : Xem bảng trạng thái toàn bộ 10 thiết bị");
    Serial.println("     - 'telemetry'                : Gửi ngay Telemetry lên MQTT (khi online)");
    Serial.println("     - 'scan'                     : Quét lại mDNS tìm MQTT Broker");
    Serial.println("     - 'restart' / 'reboot'       : Khởi động lại ESP32");
    printFingerprintHelp();
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
    Serial.println("--------------------------------------------------------------------------------------------------");
    Serial.println("THIẾT BỊ             | CHÂN GPIO | TRẠNG THÁI | THÔNG SỐ CHI TIẾT                | DEVICE ID");
    Serial.printf(" Cửa chính (Servo)   | GPIO %-3d  | %-10s | Góc: %3d° (Đích: %3d°), Tốc độ: %2dms/° | %s\n",
                  PIN_SERVO_DOOR,
                  mainDoorOpenState ? "MỞ (OPEN)" : "ĐÓNG (CLOSED)",
                  currentServoAngle, targetServoAngle, servoStepDelayMs,
                  MAIN_DOOR_DEVICE_ID);
    Serial.printf(" Đèn phòng khách     | GPIO %-3d  | %-10s | Độ sáng: %3d%% (PWM: %3d/255, Fade: %s)| %s\n",
                  PIN_LIVING_LIGHT,
                  livingLightPower ? "BẬT (ON)" : "TẮT (OFF)",
                  livingLightBrightness, (livingLightPower && livingLightBrightness > 0) ? (int)(((uint16_t)livingLightBrightness * 255) / 100) : 0,
                  enableLivingLightFading ? "ON" : "OFF",
                  LIVING_LIGHT_DEVICE_ID);
    Serial.printf(" Đèn garage (PIR)    | GPIO %-3d  | %-10s | Độ sáng: %3d%% (Auto: %s, Tắt 5s)| %s\n",
                  PIN_GARAGE_LIGHT,
                  garageLightPower ? "BẬT (ON)" : "TẮT (OFF)",
                  garageLightBrightness, autoGarageLightEnabled ? "BẬT" : "TẮT",
                  GARAGE_LIGHT_DEVICE_ID);
    Serial.printf(" Quạt thông minh     | GPIO %-3d  | %-10s | Cấp tốc độ: %d (Relay: %s)        | %s\n",
                  PIN_FAN,
                  fanPower ? "BẬT (ON)" : "TẮT (OFF)",
                  fanSpeed, fanPower ? "ACTIVE" : "INACTIVE",
                  FAN_DEVICE_ID);
    Serial.printf(" Máy bơm nước        | GPIO %-3d  | %-10s | Trạng thái Relay: %-15s| %s\n",
                  PIN_PUMP,
                  pumpPower ? "BẬT (ON)" : "TẮT (OFF)",
                  pumpPower ? "ACTIVE" : "INACTIVE",
                  PUMP_DEVICE_ID);
    Serial.printf(" Cảm biến ánh sáng   | GPIO %-3d  | %-10s | Mức logic: %d (%s)               | %s\n",
                  PIN_LDR,
                  isDark() ? "TỐI (DARK)" : "SÁNG (LIGHT)",
                  digitalRead(PIN_LDR), isDark() ? "Kích hoạt PIR" : "Khóa PIR",
                  LDR_DEVICE_ID);
    Serial.printf(" Cảm biến chuyển động| GPIO %-3d  | %-10s | Trạng thái: %-21s| %s\n",
                  PIN_PIR,
                  (digitalRead(PIN_PIR) == HIGH) ? "MOTION (Phát hiện)" : "NO MOTION (Không)",
                  PIR_DEVICE_ID);
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
        Serial.printf(" Cảm biến nhiệt ẩm   | GPIO %-3d  | %-10s | Nhiệt độ: %.1f°C | Độ ẩm: %.1f%%     | %s\n",
                      PIN_DHT, "ACTIVE", t, h, DHT_DEVICE_ID);
    } else {
        Serial.printf(" Cảm biến nhiệt ẩm   | GPIO %-3d  | %-10s | Lỗi đọc cảm biến DHT22           | %s\n",
                      PIN_DHT, "ERROR", DHT_DEVICE_ID);
    }
    bool flame = isFlameDetected();
    Serial.printf(" Cảm biến lửa        | GPIO %-3d  | %-10s | Trạng thái: %-21s| %s\n",
                  PIN_FLAME,
                  flame ? "CÓ LỬA" : "AN TOÀN",
                  flame ? ">>> BÁO CHÁY! <<<" : "Bình thường",
                  FLAME_DEVICE_ID);
    bool smoke = isSmokeDetected();
    Serial.printf(" Cảm biến khói       | GPIO %-3d  | %-10s | Trạng thái: %-21s| %s\n",
                  PIN_SMOKE,
                  smoke ? "CÓ KHÓI" : "AN TOÀN",
                  smoke ? ">>> BÁO KHÓI/GAS! <<<" : "Bình thường",
                  SMOKE_DEVICE_ID);
    Serial.printf(" Vân tay AS608       | GPIO %d/%d | %-10s | Chế độ: %-8s Quét: %-3s Tự đóng: %lus | %s\n",
                  PIN_FP_RX, PIN_FP_TX,
                  fpReady ? "SẴN SÀNG" : "LỖI/CHƯA",
                  fpMode == FP_MODE_ENROLL ? "ĐĂNG KÝ" : "QUÉT",
                  fpScanEnabled ? "BẬT" : "TẮT",
                  (unsigned long)(fpAutoCloseMs / 1000),
                  MAIN_DOOR_DEVICE_ID);
    Serial.println("--------------------------------------------------------------------------------------------------\n");
}

void handleSerialCommands() {
    if (Serial.available() <= 0) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    String lowerCmd = cmd;
    lowerCmd.toLowerCase();

    // 1. Trợ giúp và trạng thái
    if (lowerCmd == "help" || lowerCmd == "?") {
        printHelp();
        return;
    }

    if (lowerCmd == "status" || lowerCmd == "info" || lowerCmd == "s") {
        printStatus();
        return;
    }

    if (lowerCmd == "telemetry") {
        if (!offlineTestMode) {
            publishAllTelemetries();
            readAndPublishDHT();
            Serial.println("[MANUAL] Da gui Telemetry cua toan bo 8 thiet bi len MQTT.");
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

    // 2b. Cảm biến vân tay AS608 (lệnh 'fp ...')
    if (handleFingerprintCommand(lowerCmd)) return;

    // 3. Điều khiển Cửa chính (Servo SG90)
    if (lowerCmd == "door open" || lowerCmd == "mo cua" || lowerCmd == "open" || lowerCmd == "8") {
        mainDoorOpen();
        return;
    }

    if (lowerCmd == "door close" || lowerCmd == "dong cua" || lowerCmd == "close" || lowerCmd == "9") {
        mainDoorClose();
        return;
    }

    if (lowerCmd == "door stop" || lowerCmd == "stop") {
        targetServoAngle = currentServoAngle;
        publishMainDoorState();
        Serial.printf("[MAIN DOOR] Dung cua tai goc %d do\n", currentServoAngle);
        return;
    }

    if (lowerCmd.startsWith("door ") || lowerCmd.startsWith("servo ")) {
        int spaceIdx = lowerCmd.indexOf(' ');
        int ang = lowerCmd.substring(spaceIdx + 1).toInt();
        setMainDoorPosition(ang);
        return;
    }

    if (lowerCmd.startsWith("speed ") || lowerCmd.startsWith("servo_speed ") || lowerCmd.startsWith("toc do ")) {
        int spaceIdx = lowerCmd.indexOf(' ');
        int delayMs = lowerCmd.substring(spaceIdx + 1).toInt();
        setServoSpeed((uint16_t)delayMs);
        return;
    }

    // 4. Điều khiển Đèn phòng khách (GPIO 7)
    if (lowerCmd == "light on" || lowerCmd == "bat den" || lowerCmd == "living on" || lowerCmd == "bat den phong khach" || lowerCmd == "6") {
        setLivingLightPower(true);
        return;
    }

    if (lowerCmd == "light off" || lowerCmd == "tat den" || lowerCmd == "living off" || lowerCmd == "tat den phong khach" || lowerCmd == "7") {
        setLivingLightPower(false);
        return;
    }

    if (lowerCmd == "light toggle" || lowerCmd == "living toggle" || lowerCmd == "toggle") {
        toggleLivingLight();
        return;
    }

    if (lowerCmd.startsWith("living bri ") || lowerCmd.startsWith("light bri ") || lowerCmd.startsWith("bri ") || lowerCmd.startsWith("b ")) {
        int spaceIdx = lowerCmd.lastIndexOf(' ');
        int val = lowerCmd.substring(spaceIdx + 1).toInt();
        setLivingLightBrightness(val);
        return;
    }

    if (lowerCmd == "+" || lowerCmd == "tang") {
        stepLivingLightBrightness(10);
        return;
    }

    if (lowerCmd == "-" || lowerCmd == "giam") {
        stepLivingLightBrightness(-10);
        return;
    }

    if (lowerCmd.startsWith("fade") || lowerCmd.startsWith("living fade")) {
        int spaceIdx = lowerCmd.lastIndexOf(' ');
        String sub = (spaceIdx >= 0) ? lowerCmd.substring(spaceIdx + 1) : "";
        sub.trim();
        if (sub == "on" || sub == "1" || sub == "true" || sub == "enable") {
            enableLivingLightFading = true;
            Serial.println("[CONFIG] Da BAT hieu ung Smooth Fading cho den phong khach (Fade on / Fade off).");
        } else if (sub == "off" || sub == "0" || sub == "false" || sub == "disable") {
            enableLivingLightFading = false;
            livingLightCurrentDuty = livingLightTargetDuty;
            writeLivingLightPWM((uint8_t)round(livingLightCurrentDuty));
            Serial.println("[CONFIG] Da TAT Smooth Fading cho den phong khach (Do sang thay doi tuc thi).");
        } else {
            enableLivingLightFading = !enableLivingLightFading;
            Serial.printf("[CONFIG] Smooth Fading den phong khach: %s\n", enableLivingLightFading ? "ON" : "OFF");
        }
        return;
    }

    // 5. Điều khiển Đèn garage (GPIO 10)
    if (lowerCmd == "garage on" || lowerCmd == "bat den garage" || lowerCmd == "den garage on" || lowerCmd == "g on") {
        setGarageLightPower(true);
        return;
    }

    if (lowerCmd == "garage off" || lowerCmd == "tat den garage" || lowerCmd == "den garage off" || lowerCmd == "g off") {
        setGarageLightPower(false);
        return;
    }

    if (lowerCmd == "garage toggle" || lowerCmd == "toggle garage" || lowerCmd == "g toggle") {
        toggleGarageLight();
        return;
    }

    if (lowerCmd.startsWith("garage bri ") || lowerCmd.startsWith("gbri ")) {
        int spaceIdx = lowerCmd.lastIndexOf(' ');
        int val = lowerCmd.substring(spaceIdx + 1).toInt();
        setGarageLightBrightness(val);
        return;
    }

    if (lowerCmd == "auto light on" || lowerCmd == "autolight on" || lowerCmd == "bat auto light") {
        autoGarageLightEnabled = true;
        saveDeviceStatesToNVS();
        Serial.println("[AUTO LIGHT] Da BAT tinh nang tu dong bat den garage khi co chuyen dong & tu dong tat sau 5s khong chuyen dong.");
        publishGarageLightState();
        return;
    }

    if (lowerCmd == "auto light off" || lowerCmd == "autolight off" || lowerCmd == "tat auto light") {
        autoGarageLightEnabled = false;
        saveDeviceStatesToNVS();
        Serial.println("[AUTO LIGHT] Da TAT tinh nang tu dong den garage theo PIR.");
        publishGarageLightState();
        return;
    }

    // 6. Điều khiển Quạt thông minh (Fan - GPIO 19)
    if (lowerCmd == "fan on" || lowerCmd == "bat quat" || lowerCmd == "quat on" || lowerCmd == "1") {
        setFanPower(true);
        return;
    }

    if (lowerCmd == "fan off" || lowerCmd == "tat quat" || lowerCmd == "quat off" || lowerCmd == "0") {
        setFanPower(false);
        return;
    }

    if (lowerCmd == "fan toggle" || lowerCmd == "toggle fan") {
        toggleFan();
        return;
    }

    if (lowerCmd.startsWith("fan speed ") || lowerCmd.startsWith("fan ")) {
        int spaceIdx = lowerCmd.lastIndexOf(' ');
        int spd = lowerCmd.substring(spaceIdx + 1).toInt();
        if (spd >= 0 && spd <= 3) {
            setFanSpeed((uint8_t)spd);
            return;
        }
    }

    // 7. Điều khiển Máy bơm nước (Pump / Motor - GPIO 18)
    if (lowerCmd == "pump on" || lowerCmd == "bat bom" || lowerCmd == "bom on" || lowerCmd == "motor on" || lowerCmd == "2") {
        setPumpPower(true);
        return;
    }

    if (lowerCmd == "pump off" || lowerCmd == "tat bom" || lowerCmd == "bom off" || lowerCmd == "motor off" || lowerCmd == "3") {
        setPumpPower(false);
        return;
    }

    if (lowerCmd == "pump toggle" || lowerCmd == "toggle pump" || lowerCmd == "toggle motor") {
        togglePump();
        return;
    }

    // 8. Điều khiển đồng thời tất cả thiết bị
    if (lowerCmd == "all on" || lowerCmd == "bat het" || lowerCmd == "a") {
        setLivingLightPower(true);
        setGarageLightPower(true);
        setFanPower(true);
        setPumpPower(true);
        Serial.println("[ALL] Da BAT toan bo Den phong khach, Den garage, Quat va May bom nuoc!");
        return;
    }

    if (lowerCmd == "all off" || lowerCmd == "tat het" || lowerCmd == "b") {
        setLivingLightPower(false);
        setGarageLightPower(false);
        setFanPower(false);
        setPumpPower(false);
        Serial.println("[ALL] Da TAT toan bo Den phong khach, Den garage, Quat va May bom nuoc!");
        return;
    }

    // 9. Cảm biến môi trường & An toàn PCCC
    if (lowerCmd == "sensors" || lowerCmd == "sensor" || lowerCmd == "dht" || lowerCmd == "ldr" || lowerCmd == "cam bien") {
        printSensorsSerial();
        readAndPublishLDR();
        readAndPublishPIR();
        readAndPublishFlame();
        readAndPublishSmoke();
        readAndPublishDHT();
        return;
    }

    if (lowerCmd == "flame" || lowerCmd == "lua") {
        readAndPublishFlame();
        return;
    }

    if (lowerCmd == "smoke" || lowerCmd == "khoi") {
        readAndPublishSmoke();
        return;
    }

    Serial.println("[ERROR] Lenh khong xac dinh! Go 'help' de xem danh sach lenh hop le.");
}

// ====================================================================================================
// 18. ARDUINO SETUP & LOOP
// ====================================================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n====================================================================");
    Serial.println("  HESTA / SYNA SMART HOME - ESP32 8 DEVICES CONTROLLER");
    Serial.println("  NODE ID : " NODE_ID);
    Serial.println("  FIRMWARE: " FIRMWARE_VERSION);
    Serial.println("====================================================================");

    // 1. Khởi tạo chân phần cứng (Cảm biến, 2 Đèn PWM, Servo SG90, Quạt Relay, Bơm Relay)
    initHardwarePins();

    // 2. Khôi phục trạng thái từ Flash NVS
    loadDeviceStatesFromNVS();
    loadNetworkConfigFromNVS();

    // 2b. Khởi tạo cảm biến vân tay AS608 (UART1: RX=GPIO16, TX=GPIO17)
    initFingerprint();

    // 3. Kiểm tra chế độ chạy (Offline Test vs Online đầy đủ)
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

    // 1. Cập nhật hiệu ứng Smooth Fading mềm mại cho đèn phòng khách (Fade On / Fade Off sáng dần, tối dần)
    if (now - lastFadingUpdate >= 15) {
        lastFadingUpdate = now;
        updateLivingLightSmoothFading();
    }

    // 2. Cập nhật chuyển động cho Servo Cửa chính (nếu speed > 0 soft-motion, hoặc đã mở tức thì khi speed = 0)
    updateServoMotion();

    // 2b. Cảm biến vân tay: quét mở khóa / đăng ký (non-blocking) + tự đóng cửa
    updateFingerprint();

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

    // 3. Logic tự động bật ĐÈN GARAGE khi có chuyển động trong bóng tối (PIR + LDR)
    //    (Đèn phòng khách hoàn toàn giữ nguyên, không bị ảnh hưởng)
    checkMotionAndControlLight();

    // 4. Kiểm tra cảnh báo Lửa và Khói tức thì (Edge Detection)
    checkFlameAndSmokeAlert();

    // 5. Đọc cảm biến định kỳ
    if (now - lastLDRTime >= LDR_INTERVAL) {
        lastLDRTime = now;
        readAndPublishLDR();
    }

    if (now - lastPIRTime >= PIR_INTERVAL) {
        lastPIRTime = now;
        readAndPublishPIR();
    }

    if (now - lastFlameTime >= FLAME_INTERVAL) {
        lastFlameTime = now;
        readAndPublishFlame();
    }

    if (now - lastSmokeTime >= SMOKE_INTERVAL) {
        lastSmokeTime = now;
        readAndPublishSmoke();
    }

    if (now - lastDHTTime >= DHT_INTERVAL) {
        lastDHTTime = now;
        readAndPublishDHT();
    }

    // 6. Xử lý các lệnh tương tác từ Serial Monitor
    handleSerialCommands();
}
