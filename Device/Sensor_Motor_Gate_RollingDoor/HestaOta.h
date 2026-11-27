#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <esp_ota_ops.h>

/**
 * HestaOta.h - Production ESP32 OTA & Diagnostics Ping/Pong Module
 *
 * Safeguards:
 *  - Owns String copies of nodeId & firmwareVersion (safe when sketch mutates String IDs)
 *  - Auto-upgrades PubSubClient packet buffer to >= 1024 bytes when initialized
 *  - Deferred execution in loop() (never calls HTTP/Update/publish inside mqttCallback)
 *  - Idempotent campaign handling against dual-topic (nodeCode + UUID) / QoS-1 redelivery
 *  - Pre-flash hardware fail-safe hook (stops motors/pumps/BLE advertising before Update.begin)
 *  - Supports HTTP & HTTPS with up to 5 manual cross-protocol redirects (keeps HTTP/1.1 CDN compatibility)
 *  - Supports fixed Content-Length and Transfer-Encoding: chunked via Stream sink
 *  - Emits 15s DOWNLOADING heartbeats to keep DeadMansSnitch & OTA Watchdog alive during slow downloads
 *  - Verifies ESP32 firmware magic byte 0xE9 and incremental mbedTLS SHA-256 checksum
 *  - Persists pending OTA state to NVS and confirms/detects bootloader rollback after ESP.restart()
 */

typedef void (*HestaOtaActionFn)();

struct HestaOtaPendingJob {
  bool queued = false;
  bool inProgress = false;
  String campaignId;
  String version;
  String fileUrl;
  String sha256;
};

static PubSubClient* _hestaOtaMqtt = nullptr;
static String _hestaOtaNodeId = "";
static String _hestaOtaFwVersion = "1.0.0";
static HestaOtaActionFn _hestaOtaPreStopFn = nullptr;
static HestaOtaActionFn _hestaOtaPostFailFn = nullptr;
static HestaOtaPendingJob _hestaOtaJob;
static String _hestaOtaAbortedCampaignId = "";
static String _hestaOtaLastCompletedCampaignId = "";

static String _hestaOtaTopicPrefix() {
  return String("hesta/nodes/") + _hestaOtaNodeId;
}

static String _hestaOtaEscapeJson(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += " ";
    else if (c == '\r') continue;
    else out += c;
  }
  return out;
}

static String hestaOtaExtractJsonString(const String& json, const char* key) {
  String needle = String("\"") + key + "\"";
  int idx = json.indexOf(needle);
  if (idx < 0) return "";
  int colon = json.indexOf(':', idx + needle.length());
  if (colon < 0) return "";
  int q1 = json.indexOf('\"', colon + 1);
  if (q1 < 0) return "";
  int q2 = q1 + 1;
  while (q2 < (int)json.length()) {
    if (json.charAt(q2) == '\"' && json.charAt(q2 - 1) != '\\') break;
    q2++;
  }
  if (q2 >= (int)json.length()) return "";
  String val = json.substring(q1 + 1, q2);
  val.replace("\\/", "/");
  return val;
}

static void hestaOtaPublishStatus(const String& campaignId, const String& version, const String& status, const String& err) {
  if (!_hestaOtaMqtt || !_hestaOtaMqtt->connected() || _hestaOtaNodeId.length() == 0) return;
  String payload = "{\"campaignId\":\"" + _hestaOtaEscapeJson(campaignId)
      + "\",\"nodeCode\":\"" + _hestaOtaEscapeJson(_hestaOtaNodeId)
      + "\",\"nodeId\":\"" + _hestaOtaEscapeJson(_hestaOtaNodeId)
      + "\",\"version\":\"" + _hestaOtaEscapeJson(version)
      + "\",\"reportedVersion\":\"" + _hestaOtaEscapeJson(_hestaOtaFwVersion)
      + "\",\"status\":\"" + _hestaOtaEscapeJson(status)
      + "\",\"error\":\"" + _hestaOtaEscapeJson(err) + "\"}";
  _hestaOtaMqtt->publish((_hestaOtaTopicPrefix() + "/ota/status").c_str(), payload.c_str(), false);
  _hestaOtaMqtt->loop();
}

static void _hestaOtaSubscribeTopics() {
  if (!_hestaOtaMqtt || !_hestaOtaMqtt->connected() || _hestaOtaNodeId.length() == 0) return;
  String prefix = _hestaOtaTopicPrefix();
  _hestaOtaMqtt->subscribe((prefix + "/ping").c_str(), 1);
  _hestaOtaMqtt->subscribe((prefix + "/ota/notify").c_str(), 1);
  _hestaOtaMqtt->subscribe((prefix + "/ota/abort").c_str(), 1);
}

static void hestaOtaSetNodeId(const String& nodeId) {
  String clean = nodeId;
  clean.trim();
  if (clean.length() == 0 || clean == _hestaOtaNodeId) return;
  _hestaOtaNodeId = clean;
  _hestaOtaSubscribeTopics();
}

static bool _hestaOtaSavePendingNvs(const String& campaignId, const String& version, uint32_t prePartitionAddr) {
  Preferences prefs;
  if (!prefs.begin("hesta_ota", false)) return false;
  size_t w1 = prefs.putString("camp", campaignId);
  size_t w2 = prefs.putString("ver", version);
  size_t w3 = prefs.putUInt("addr", prePartitionAddr);
  prefs.end();
  return (w1 > 0 && w2 > 0 && w3 > 0);
}

static void _hestaOtaClearPendingNvs() {
  Preferences prefs;
  if (prefs.begin("hesta_ota", false)) {
    prefs.clear();
    prefs.end();
  }
}

static void hestaOtaInit(
    PubSubClient* mqttClient,
    const char* nodeId,
    const char* firmwareVersion,
    HestaOtaActionFn preFlashFailSafeFn = nullptr,
    HestaOtaActionFn postFlashFailFn = nullptr) {
  _hestaOtaMqtt = mqttClient;
  if (_hestaOtaMqtt && _hestaOtaMqtt->getBufferSize() < 1024) {
    _hestaOtaMqtt->setBufferSize(1024);
  }
  _hestaOtaNodeId = nodeId ? String(nodeId) : String("");
  _hestaOtaNodeId.trim();
  _hestaOtaFwVersion = (firmwareVersion && strlen(firmwareVersion) > 0) ? String(firmwareVersion) : String("1.0.0");
  _hestaOtaFwVersion.trim();
  _hestaOtaPreStopFn = preFlashFailSafeFn;
  _hestaOtaPostFailFn = postFlashFailFn;
}

static void hestaOtaOnMqttConnected() {
  if (!_hestaOtaMqtt || !_hestaOtaMqtt->connected() || _hestaOtaNodeId.length() == 0) return;

  _hestaOtaSubscribeTopics();
  esp_ota_mark_app_valid_cancel_rollback();

  Preferences prefs;
  // Use read/write mode so fresh NVS creates the namespace cleanly without NOT_FOUND errors.
  if (prefs.begin("hesta_ota", false)) {
    String pendingCamp = prefs.getString("camp", "");
    String pendingVer = prefs.getString("ver", "");
    uint32_t preAddr = prefs.getUInt("addr", 0);
    prefs.end();

    if (pendingCamp.length() > 0) {
      const esp_partition_t* running = esp_ota_get_running_partition();
      uint32_t nowAddr = running ? running->address : 0;
      _hestaOtaClearPendingNvs();

      if (preAddr != 0 && nowAddr == preAddr) {
        Serial.printf("[HESTA-OTA] Bootloader rollback detected (addr=0x%lx)! Reporting FAILED for %s\n",
                      (unsigned long)nowAddr, pendingCamp.c_str());
        hestaOtaPublishStatus(pendingCamp, pendingVer, "FAILED",
                              "Bootloader rollback: firmware moi khoi dong that bai");
      } else {
        Serial.printf("[HESTA-OTA] Post-reboot OTA verified (addr=0x%lx, fw=%s)! Reporting SUCCESS for %s\n",
                      (unsigned long)nowAddr, _hestaOtaFwVersion.c_str(), pendingCamp.c_str());
        _hestaOtaLastCompletedCampaignId = pendingCamp;
        hestaOtaPublishStatus(pendingCamp, pendingVer, "SUCCESS", "");
      }
    }
  }
}

/**
 * Call inside mqttCallback() BEFORE device-command parsing.
 * Copies payload fields cleanly and avoids reentrant publish/Update inside callback.
 * Returns true if the topic was handled (/ping, /ota/notify, /ota/abort).
 */
static bool hestaOtaHandleMqttMessage(const char* topic, const byte* payload, unsigned int length) {
  if (!topic || _hestaOtaNodeId.length() == 0) return false;
  String topicStr(topic);
  String prefix = _hestaOtaTopicPrefix();

  bool isPing = (topicStr == prefix + "/ping");
  bool isAbort = (topicStr == prefix + "/ota/abort");
  bool isNotify = (topicStr == prefix + "/ota/notify");
  if (!isPing && !isAbort && !isNotify) return false;

  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (isPing) {
    String pingId = hestaOtaExtractJsonString(msg, "pingId");
    String pong = "{\"pingId\":\"" + _hestaOtaEscapeJson(pingId)
        + "\",\"nodeCode\":\"" + _hestaOtaEscapeJson(_hestaOtaNodeId)
        + "\",\"nodeId\":\"" + _hestaOtaEscapeJson(_hestaOtaNodeId)
        + "\",\"firmwareVersion\":\"" + _hestaOtaEscapeJson(_hestaOtaFwVersion) + "\"}";
    if (_hestaOtaMqtt && _hestaOtaMqtt->connected()) {
      _hestaOtaMqtt->publish((prefix + "/pong").c_str(), pong.c_str(), false);
    }
    return true;
  }

  if (isAbort) {
    String campId = hestaOtaExtractJsonString(msg, "campaignId");
    if (campId.length() > 0) {
      _hestaOtaAbortedCampaignId = campId;
      if (_hestaOtaJob.queued && _hestaOtaJob.campaignId == campId) {
        _hestaOtaJob.queued = false;
      }
    }
    Serial.printf("[HESTA-OTA] Abort received for campaign %s\n", campId.c_str());
    return true;
  }

  // /ota/notify -> queue for execution in loop()
  String campId = hestaOtaExtractJsonString(msg, "campaignId");
  String version = hestaOtaExtractJsonString(msg, "version");
  String fileUrl = hestaOtaExtractJsonString(msg, "fileUrl");
  String sha256 = hestaOtaExtractJsonString(msg, "checksumSha256");

  if (fileUrl.length() == 0) return true;

  if (_hestaOtaJob.inProgress || _hestaOtaJob.queued) {
    Serial.printf("[HESTA-OTA] Ignoring duplicate/overlapping /ota/notify while busy (camp=%s)\n", campId.c_str());
    return true;
  }
  if (campId.length() > 0 && campId == _hestaOtaLastCompletedCampaignId) {
    Serial.printf("[HESTA-OTA] Ignoring already completed campaign %s\n", campId.c_str());
    return true;
  }

  _hestaOtaJob.campaignId = campId;
  _hestaOtaJob.version = version;
  _hestaOtaJob.fileUrl = fileUrl;
  _hestaOtaJob.sha256 = sha256;
  _hestaOtaJob.queued = true;
  Serial.printf("[HESTA-OTA] Queued OTA campaign=%s version=%s url=%s\n",
                campId.c_str(), version.c_str(), fileUrl.c_str());
  return true;
}

class HestaOtaChunkedSink : public Stream {
 public:
  HestaOtaChunkedSink(
      const String& campaignId,
      const String& version,
      mbedtls_md_context_t* shaCtx)
      : _campaignId(campaignId),
        _version(version),
        _shaCtx(shaCtx),
        _lastReportMs(millis()) {}

  size_t totalWritten = 0;
  bool aborted = false;
  bool failed = false;
  String errorMessage = "";

  size_t write(uint8_t b) override {
    return write(&b, 1);
  }

  size_t write(const uint8_t* data, size_t len) override {
    if (_hestaOtaMqtt && _hestaOtaMqtt->connected()) {
      _hestaOtaMqtt->loop();
    }
    yield();

    if (_campaignId.length() > 0 && _hestaOtaAbortedCampaignId == _campaignId) {
      aborted = true;
      errorMessage = "Aborted by Admin during download";
      return 0;
    }

    if (len == 0) return 0;

    if (totalWritten == 0 && data[0] != 0xE9) {
      failed = true;
      errorMessage = "Invalid ESP32 binary magic byte (expected 0xE9)";
      return 0;
    }

    mbedtls_md_update(_shaCtx, data, len);
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
      failed = true;
      errorMessage = "Flash write error";
      return 0;
    }

    totalWritten += len;
    if ((unsigned long)(millis() - _lastReportMs) >= 15000UL && totalWritten > _lastReportedBytes) {
      _lastReportedBytes = totalWritten;
      _lastReportMs = millis();
      hestaOtaPublishStatus(_campaignId, _version, "DOWNLOADING", "");
    }
    return len;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  String _campaignId;
  String _version;
  mbedtls_md_context_t* _shaCtx;
  unsigned long _lastReportMs;
  size_t _lastReportedBytes = 0;
};

static void _hestaOtaExecuteJob(const HestaOtaPendingJob& job) {
  if (job.campaignId.length() > 0 && _hestaOtaAbortedCampaignId == job.campaignId) {
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Campaign aborted before start");
    return;
  }

  // 1. Hardware safety hook (stop motors/pumps & release shared BLE radio / heap)
  if (_hestaOtaPreStopFn) {
    _hestaOtaPreStopFn();
  }

  Serial.printf("[HESTA-OTA] Free heap before download: %u bytes\n", (unsigned int)ESP.getFreeHeap());
  hestaOtaPublishStatus(job.campaignId, job.version, "DOWNLOADING", "");

  String currentUrl = job.fileUrl;
  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  int httpCode = -1;
  bool requestReady = false;

  // Handle up to 5 HTTP/HTTPS redirects manually so protocol switches (http <-> https) reset TLS/TCP clients cleanly.
  for (int hop = 0; hop < 5; hop++) {
    http.end();
    plainClient.stop();
    secureClient.stop();

    http.setTimeout(25000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* hdrs[] = {"Location"};
    http.collectHeaders(hdrs, 1);

    bool began = false;
    if (currentUrl.startsWith("https://")) {
      secureClient.setInsecure();
      secureClient.setTimeout(25);
      began = http.begin(secureClient, currentUrl);
    } else if (currentUrl.startsWith("http://")) {
      began = http.begin(plainClient, currentUrl);
    }

    if (!began) {
      http.end();
      hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Cannot open firmware URL");
      if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
      return;
    }

    httpCode = http.GET();
    if (httpCode == 301 || httpCode == 302 || httpCode == 303 || httpCode == 307 || httpCode == 308) {
      String nextLoc = http.header("Location");
      nextLoc.trim();
      http.end();
      if (nextLoc.length() == 0) {
        hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Redirect without Location header");
        if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
        return;
      }
      if (nextLoc.startsWith("/")) {
        int schemeEnd = currentUrl.indexOf("://");
        int pathStart = (schemeEnd >= 0) ? currentUrl.indexOf('/', schemeEnd + 3) : -1;
        String origin = (pathStart >= 0) ? currentUrl.substring(0, pathStart) : currentUrl;
        currentUrl = origin + nextLoc;
      } else {
        currentUrl = nextLoc;
      }
      continue;
    }

    requestReady = true;
    break;
  }

  if (!requestReady) {
    http.end();
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Too many HTTP redirects");
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "HTTP error code " + String(httpCode));
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  int contentLen = http.getSize();
  size_t beginSize = (contentLen > 0) ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(beginSize)) {
    http.end();
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Not enough OTA flash partition space");
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  mbedtls_md_context_t shaCtx;
  mbedtls_md_init(&shaCtx);
  mbedtls_md_setup(&shaCtx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&shaCtx);

  size_t totalWritten = 0;

  if (contentLen > 0) {
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    unsigned long lastDataMs = millis();
    unsigned long lastReportMs = lastDataMs;
    size_t lastReportedBytes = 0;

    while (totalWritten < (size_t)contentLen) {
      if (_hestaOtaMqtt && _hestaOtaMqtt->connected()) {
        _hestaOtaMqtt->loop();
      }
      yield();

      if (job.campaignId.length() > 0 && _hestaOtaAbortedCampaignId == job.campaignId) {
        Update.abort();
        mbedtls_md_free(&shaCtx);
        http.end();
        hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Aborted by Admin during download");
        if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
        return;
      }

      size_t avail = stream->available();
      if (avail > 0) {
        size_t remaining = (size_t)contentLen - totalWritten;
        size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
        if (toRead > remaining) toRead = remaining;
        int readLen = stream->readBytes(buf, toRead);
        if (readLen > 0) {
          lastDataMs = millis();
          if (totalWritten == 0 && buf[0] != 0xE9) {
            Update.abort();
            mbedtls_md_free(&shaCtx);
            http.end();
            hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Invalid ESP32 binary magic byte (expected 0xE9)");
            if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
            return;
          }
          mbedtls_md_update(&shaCtx, buf, readLen);
          if (Update.write(buf, readLen) != (size_t)readLen) {
            Update.abort();
            mbedtls_md_free(&shaCtx);
            http.end();
            hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Flash write error");
            if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
            return;
          }
          totalWritten += readLen;
          if ((unsigned long)(millis() - lastReportMs) >= 15000UL && totalWritten > lastReportedBytes) {
            lastReportedBytes = totalWritten;
            lastReportMs = millis();
            hestaOtaPublishStatus(job.campaignId, job.version, "DOWNLOADING", "");
          }
        }
      } else {
        if (!http.connected()) {
          break;
        }
        if ((unsigned long)(millis() - lastDataMs) > 25000UL) {
          Update.abort();
          mbedtls_md_free(&shaCtx);
          http.end();
          hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Firmware download timeout");
          if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
          return;
        }
        delay(5);
      }
    }
  } else {
    // Unknown length / Transfer-Encoding: chunked -> let HTTPClient::writeToStream decode chunk framing cleanly.
    HestaOtaChunkedSink sink(job.campaignId, job.version, &shaCtx);
    int streamed = http.writeToStream(&sink);
    totalWritten = sink.totalWritten;
    if (sink.aborted || sink.failed || streamed < 0) {
      String errMsg = sink.errorMessage.length() > 0
          ? sink.errorMessage
          : ("Chunked stream error (" + String(streamed) + ")");
      Update.abort();
      mbedtls_md_free(&shaCtx);
      http.end();
      hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", errMsg);
      if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
      return;
    }
  }

  hestaOtaPublishStatus(job.campaignId, job.version, "VERIFYING", "");
  uint8_t digest[32];
  mbedtls_md_finish(&shaCtx, digest);
  mbedtls_md_free(&shaCtx);
  http.end();

  if (totalWritten == 0 || (contentLen > 0 && totalWritten != (size_t)contentLen)) {
    Update.abort();
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Incomplete firmware binary downloaded");
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  char hexDigest[65];
  for (int i = 0; i < 32; i++) {
    sprintf(&hexDigest[i * 2], "%02x", digest[i]);
  }
  hexDigest[64] = '\0';

  if (job.sha256.length() == 64 && !job.sha256.equalsIgnoreCase(String(hexDigest))) {
    Update.abort();
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "SHA-256 checksum mismatch");
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  hestaOtaPublishStatus(job.campaignId, job.version, "FLASHING", "");
  const esp_partition_t* currentPart = esp_ota_get_running_partition();
  uint32_t preAddr = currentPart ? currentPart->address : 0;

  if (!Update.end(true)) {
    hestaOtaPublishStatus(job.campaignId, job.version, "FAILED", "Update.end finalize failed");
    if (_hestaOtaPostFailFn) _hestaOtaPostFailFn();
    return;
  }

  bool nvsSaved = _hestaOtaSavePendingNvs(job.campaignId, job.version, preAddr);
  Serial.printf("[HESTA-OTA] Flash complete! NVS pending saved=%s, preAddr=0x%lx. Rebooting...\n",
                nvsSaved ? "true" : "false", (unsigned long)preAddr);

  _hestaOtaLastCompletedCampaignId = job.campaignId;
  hestaOtaPublishStatus(job.campaignId, job.version, "REBOOTING", "");
  if (!nvsSaved) {
    hestaOtaPublishStatus(job.campaignId, job.version, "SUCCESS", "");
  }
  delay(400);
  ESP.restart();
}

static void hestaOtaLoop() {
  if (!_hestaOtaJob.queued || _hestaOtaJob.inProgress) return;
  HestaOtaPendingJob job = _hestaOtaJob;
  _hestaOtaJob.queued = false;
  _hestaOtaJob.inProgress = true;
  _hestaOtaExecuteJob(job);
  _hestaOtaJob.inProgress = false;
}
