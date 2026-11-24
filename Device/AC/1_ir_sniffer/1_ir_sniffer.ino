/*
 * BƯỚC 1: Đọc remote Daikin để biết giao thức (protocol) của điều hòa.
 * Phần cứng: module thu IR (VS1838B / TSOP1838) -> OUT nối GPIO 5, VCC 3.3V, GND.
 * Thư viện: IRremoteESP8266 (cài qua Library Manager).
 * Mở Serial Monitor 115200, hướng remote Daikin vào module thu và bấm nút.
 */
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>

const uint16_t kRecvPin = 5;           // chân nối module thu IR
const uint16_t kCaptureBufferSize = 1024;  // Daikin có khung rất dài
const uint8_t  kTimeout = 50;          // ms, đủ dài để gom trọn 1 bản tin Daikin
const uint16_t kMinUnknownSize = 12;

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

void setup() {
  Serial.begin(115200);
  delay(1000);
  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.enableIRIn();
  Serial.println("San sang. Bam nut tren remote Daikin...");
}

void loop() {
  if (irrecv.decode(&results)) {
    Serial.println("==================================");
    Serial.print("Protocol : ");
    Serial.println(typeToString(results.decode_type));
    Serial.print("So bit   : ");
    Serial.println(results.bits);

    // In ra trạng thái dễ đọc (nhiệt độ, chế độ, quạt...) nếu là AC được hỗ trợ
    String s = IRAcUtils::resultAcToString(&results);
    if (s.length()) Serial.println("Trang thai: " + s);

    // In state bytes (dùng cho irsend.sendDaikinXXX) hoặc raw nếu UNKNOWN
    Serial.println(resultToSourceCode(&results));
    Serial.println();
    yield();
  }
}
