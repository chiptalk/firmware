#include "WifiManager.h"
#include <ESPmDNS.h>
#include <ArduinoJson.h>

// Set to 1 to print scan progress
#ifndef WIFI_SCAN_DEBUG
#define WIFI_SCAN_DEBUG 0
#endif

static constexpr const char* NVS_NS   = "net";
static constexpr const char* KEY_SSID = "ssid";
static constexpr const char* KEY_PASS = "pass";

static constexpr const char* AP_SSID = "ESP32-SETUP";
static constexpr const char* AP_PASS = "";

// -------------------- Lifecycle --------------------

void WifiManager::begin() {
  prefs_.begin(NVS_NS, false);

  uint32_t chip = (uint32_t)ESP.getEfuseMac();
  hostname_ = "esp32-" + String(chip, HEX);
  hostname_.toLowerCase();

  WiFi.mode(WIFI_OFF);
  delay(10);

  // Makes scanning + AP portal behavior more consistent on some ESP32 builds
  WiFi.setSleep(false);

  mdnsStarted_ = false;

  // Scan state
  scanRunning_ = false;
  lastScanCount_ = -1;
}

void WifiManager::loop() {
  // STA reconnect logic when STA interface is enabled (STA or AP+STA)
  wifi_mode_t mode = WiFi.getMode();
  bool staEnabled = (mode == WIFI_STA) || (mode == WIFI_AP_STA);

  //if (staEnabled) {
  if (staEnabled && !apSetupMode_) {

    if (!isConnected()) {
      uint32_t now = millis();
      if (now - lastStaAttemptMs_ > 5000) {
        lastStaAttemptMs_ = now;

        String ssid = read_(KEY_SSID);
        String pass = read_(KEY_PASS);

        // Only auto-reconnect if we actually have creds stored
        if (ssid.length()) {
          WiFi.disconnect(false, false);
          delay(10);
          WiFi.begin(ssid.c_str(), pass.c_str());
        }
      }
    } else {
      ensureMdnsStarted();
    }
  }

  // Scan monitor (scan is started in startScanAsync())
  if (scanRunning_) {
    int n = WiFi.scanComplete();

#if WIFI_SCAN_DEBUG
    Serial.printf("[SCAN] complete=%d\n", n);
#endif

    if (n == WIFI_SCAN_RUNNING) {
      // still running
      return;
    }

    if (n >= 0) {
      // completed successfully
      lastScanCount_ = n;
      scanRunning_ = false;
      return;
    }

    // error / aborted
    lastScanCount_ = -1;
    scanRunning_ = false;
    WiFi.scanDelete();
  }
}

// -------------------- Credentials --------------------

bool WifiManager::hasCredentials() const {
  return read_(KEY_SSID).length() > 0;
}

void WifiManager::clearCredentials() {
  prefs_.remove(KEY_SSID);
  prefs_.remove(KEY_PASS);
}

String WifiManager::savedSsid() const {
  return read_(KEY_SSID);
}

// -------------------- Modes --------------------

void WifiManager::startApSetup() {
  apSetupMode_ = true;
  mdnsStarted_ = false;

  WiFi.mode(WIFI_AP_STA);
  delay(50);

  WiFi.setAutoReconnect(false);     // ✅ important in setup mode
  WiFi.disconnect(false, true);     // stop STA + clear pending connect
  delay(10);

  IPAddress ip(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress sn(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, sn);

  WiFi.softAP(AP_SSID, AP_PASS);
}

void WifiManager::startSta() {
  apSetupMode_ = false;
  mdnsStarted_ = false;

  WiFi.mode(WIFI_STA);
  delay(50);

  WiFi.setAutoReconnect(true);      // ✅ normal mode
  WiFi.setHostname(hostname_.c_str());

  String ssid = read_(KEY_SSID);
  String pass = read_(KEY_PASS);

  lastStaAttemptMs_ = millis();
  WiFi.begin(ssid.c_str(), pass.c_str());
}


bool WifiManager::saveCredentialsAndConnect(const String& ssid, const String& pass) {
  if (!ssid.length()) return false;
  write_(KEY_SSID, ssid);
  write_(KEY_PASS, pass);
  startSta();
  return true;
}

// -------------------- Status --------------------

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::isStaMode() const {
  wifi_mode_t m = WiFi.getMode();
  return (m == WIFI_STA) || (m == WIFI_AP_STA);
}

String WifiManager::ipString() const {
  wifi_mode_t m = WiFi.getMode();
  if (m == WIFI_AP || m == WIFI_AP_STA) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

void WifiManager::ensureMdnsStarted() {
  if (mdnsStarted_) return;
  if (!isConnected()) return;

  mdnsStarted_ = MDNS.begin(hostname_.c_str());
  if (mdnsStarted_) {
    MDNS.addService("http", "tcp", 80);
  }
}

// -------------------- Scan API --------------------
/*
void WifiManager::startScanAsync() {
  if (scanRunning_) return;

  // Ensure scan-capable mode
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(20);
  }

  // ✅ Force STA idle so scan isn't competing with a connection attempt
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(20);

  WiFi.scanDelete();
  lastScanCount_ = -2;
  scanRunning_ = true;

  // Use active scan (passive=false) to avoid "0 networks" on repeated scans
  // signature: scanNetworks(async, show_hidden, passive, max_ms_per_chan, channel)
  WiFi.scanNetworks(true, true, false, 300);
}
*/
void WifiManager::startScanAsync() {
  // If already running, do nothing
  if (scanRunning_) return;

  // Ensure STA interface exists (scan needs STA)
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(20);
  }

  // Clean old results and start an async scan
  WiFi.scanDelete();
  lastScanCount_ = -2;
  scanRunning_ = true;

  // ESP32 signature: scanNetworks(async, show_hidden, passive, max_ms_per_chan, channel)
  // passive=true is usually more stable; 300ms per channel is a good start.
  WiFi.scanNetworks(true, true, true, 300);
}
  

bool WifiManager::isScanRunning() const {
  return scanRunning_;
}

bool WifiManager::hasScanResults() const {
  return (!scanRunning_ && lastScanCount_ >= 0);
}

int WifiManager::scanCount() const {
  return lastScanCount_ >= 0 ? lastScanCount_ : 0;
}

void WifiManager::clearScanResults() {
  lastScanCount_ = -1;
  WiFi.scanDelete();
}

String WifiManager::scanResultsJson() const {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.to<JsonArray>();

  // Use cached count; scanComplete() can return -2 if running or -1 if deleted
  int n = lastScanCount_;

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;

      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      bool open = (auth == WIFI_AUTH_OPEN);

      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = ssid;
      o["rssi"] = rssi;
      o["open"] = open;
      o["enc"]  = (int)auth;
    }
  }

  String out;
  serializeJson(arr, out);
  return out;
}

// -------------------- NVS helpers --------------------

String WifiManager::read_(const char* key) const {
  Preferences* p = const_cast<Preferences*>(&prefs_);
  return p->getString(key, "");
}

void WifiManager::write_(const char* key, const String& value) {
  prefs_.putString(key, value);
}
