#pragma once
#include <Arduino.h>

enum class AppMode : uint8_t {
  AP_SETUP = 0,
  STA_NORMAL = 1
};

// What the OLED should show.
enum class UiScreen : uint8_t {
  WIFI_INFO = 0,
  QR_CODE   = 1,
  SCALE     = 2
};

struct AppState {
  AppMode mode = AppMode::AP_SETUP;

  UiScreen screen = UiScreen::WIFI_INFO;

  // Future-proofing: later you can toggle this via the web UI.
  bool scaleModeEnabled = true;

  bool wifiConnected = false;
  String ip;
  String hostname;

  // set true by web handler; handled in App loop
  bool requestClearWifi = false;

  // true once NTP sync succeeds — used as internet-connectivity proxy
  bool timeSynced = false;

  // Recent comm activity (HTTP + MQTT aggregated), for OLED TX/RX indicators.
  uint32_t commTxMs = 0;
  uint32_t commRxMs = 0;
};
