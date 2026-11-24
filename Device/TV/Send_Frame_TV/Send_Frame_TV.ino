#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ===== CẤU HÌNH =====
const char* WIFI_SSID = "NGUYEN-TOAN";
const char* WIFI_PASS = "12345678";
const uint16_t kIrLedPin = 4;
// ====================

struct Btn { const char* name; uint32_t code; };

const Btn BUTTONS[] = {
  {"POWER", 0x0D5F2A}, {"MUTE", 0x0C0F3F}, {"INPUT", 0x05CFA3},
  {"VOL_UP", 0x0D0F2F}, {"VOL_DOWN", 0x0D1F2E},
  {"CH_UP", 0x0D2F2D}, {"CH_DOWN", 0x0D3F2C},
  {"UP", 0x0A6F59}, {"DOWN", 0x0A7F58},
  {"LEFT", 0x0A9F56}, {"RIGHT", 0x0A8F57}, {"OK", 0x00BFF4},
  {"BACK", 0x0D8F27}, {"HOME", 0x0F7F08}, {"MENU", 0x013FEC},
  {"0", 0x0CFF30}, {"1", 0x0CEF31}, {"2", 0x0CDF32}, {"3", 0x0CCF33},
  {"4", 0x0CBF34}, {"5", 0x0CAF35}, {"6", 0x0C9F36},
  {"7", 0x0C8F37}, {"8", 0x0C7F38}, {"9", 0x0C6F39}
};
const int N = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

IRsend irsend(kIrLedPin);
WebServer server(80);

bool sendByName(String name) {
  name.trim();
  name.toUpperCase();
  for (int i = 0; i < N; i++) {
    if (name == BUTTONS[i].name) {
      irsend.sendNikai(BUTTONS[i].code, 24, 1);
      Serial.printf("Da gui %s = 0x%06X\n", BUTTONS[i].name, BUTTONS[i].code);
      return true;
    }
  }
  return false;
}

String buildPage() {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>TCL Remote</title><style>"
               "body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:12px}"
               "button{font-size:18px;margin:4px;padding:14px 0;width:30%;border:0;border-radius:10px;background:#2a2a2a;color:#fff}"
               "button:active{background:#0a84ff}"
               ".pw{background:#c0392b}"
               "</style></head><body><h2>TCL TV Remote</h2>");
  for (int i = 0; i < N; i++) {
    h += "<button";
    if (String(BUTTONS[i].name) == "POWER") h += " class='pw'";
    h += " onclick=\"fetch('/send?b=";
    h += BUTTONS[i].name;
    h += "')\">";
    h += BUTTONS[i].name;
    h += "</button>";
    if (i == 2 || i == 6 || i == 11 || i == 14) h += "<br>";
  }
  h += "</body></html>";
  return h;
}

void handleRoot() { server.send(200, "text/html", buildPage()); }

void handleSend() {
  if (server.hasArg("b") && sendByName(server.arg("b")))
    server.send(200, "text/plain", "OK");
  else
    server.send(400, "text/plain", "Nut khong hop le");
}

void setup() {
  Serial.begin(115200);
  irsend.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.print("\nMo trinh duyet: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/send", handleSend);
  server.begin();

  Serial.println("Hoac go ten nut trong Serial (vd: power, vol_up, 5).");
}

void loop() {
  server.handleClient();

  static String line = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() && !sendByName(line)) Serial.println("Khong co nut nay.");
      line = "";
    } else line += c;
  }
}