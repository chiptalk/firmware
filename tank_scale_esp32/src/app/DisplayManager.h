#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AppState.h"
#include "WifiManager.h"

class DisplayManager {
public:
  bool begin();
  void sleep();
  void wake();
  bool isSleeping() const { return sleeping_; }
  void showBoot(const String& line);
  void showStatus(const AppState& state, const WifiManager& wifi);  // AP mode
  void showQrUrl(const String& url);
  void showMainNormal(const AppState& state, const String& weight, bool scaleActive);

private:
  Adafruit_SSD1306 oled_{128, 64, &Wire, -1};
  uint32_t lastDrawMs_ = 0;
  bool ready_ = false;
  bool sleeping_ = false;

  void drawHeader_(const String& title);
  void drawWifiIcon_(int16_t x, int16_t y, bool ok);
  void drawInternetIcon_(int16_t x, int16_t y, bool ok);
  void drawScaleIcon_(int16_t x, int16_t y, bool ok);
  void drawCommsArrows_(int16_t x, int16_t y, bool txActive, bool rxActive);
};
