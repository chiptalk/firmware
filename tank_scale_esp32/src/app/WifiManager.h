#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

class WifiManager {
public:
  void begin();
  void loop();

  bool hasCredentials() const;
  void clearCredentials();

  // AP setup mode
  void startApSetup();

  // STA normal mode
  void startSta();

  // Connection state
  bool isConnected() const;
  bool isStaMode() const;
  String ipString() const;
  String hostname() const { return hostname_; }

  // Called when user submits creds
  bool saveCredentialsAndConnect(const String& ssid, const String& pass);

  // For UI/debug
  String savedSsid() const;

  // mDNS reliability
  void ensureMdnsStarted();

  // ---- Wi-Fi scan (async/non-blocking) ----
  void startScanAsync();        // starts if not already scanning
  bool isScanRunning() const;   // true while scan is ongoing
  bool hasScanResults() const;  // true once scan completed successfully
  int  scanCount() const;       // number of networks in last result
  void clearScanResults();      // free scan results
  String scanResultsJson() const; // JSON array of networks

private:
  Preferences prefs_;
  String hostname_ = "esp32";
  bool mdnsStarted_ = false;

  uint32_t lastStaAttemptMs_ = 0;

  // Scan state
  bool apSetupMode_ = false;
  // bool scanRequested_ = false;
  bool scanRunning_ = false;
  int lastScanCount_ = -1; // -1 none, >=0 results, -2 running

  String read_(const char* key) const;
  void write_(const char* key, const String& value);

  void startMdns_();
};
