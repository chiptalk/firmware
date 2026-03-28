#include "DisplayManager.h"
#include <QRCode.h>   // from ricmoo/QRCode

static constexpr uint8_t I2C_SDA = 21; // typical ESP32 defaults
static constexpr uint8_t I2C_SCL = 22;
/*
bool DisplayManager::begin() {
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!oled_.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    return false;
  }
  oled_.clearDisplay();
  oled_.setTextSize(1);
  oled_.setTextColor(SSD1306_WHITE);
  oled_.display();
  return true;
}
  

  bool DisplayManager::begin() {
  Wire.begin();          // default ESP32 pins: SDA=21, SCL=22
  Wire.setClock(400000);

  // Try 0x3C first (most common), then 0x3D
  if (!oled_.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!oled_.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      ready_ = false;
      Serial.println("[OLED] begin failed (check wiring/address 0x3C/0x3D)");
      return false;
    }
  }

  ready_ = true;

  oled_.clearDisplay();
  oled_.setTextColor(SSD1306_WHITE);
  oled_.setTextSize(1);
  oled_.setCursor(0, 0);
  oled_.println("OLED OK");
  oled_.display();
  sleeping_ = false;

  return true;
}

void DisplayManager::sleep() {
  if (!ready_ || sleeping_) return;
  oled_.ssd1306_command(SSD1306_DISPLAYOFF);
  sleeping_ = true;
}

void DisplayManager::wake() {
  if (!ready_ || !sleeping_) return;
  oled_.ssd1306_command(SSD1306_DISPLAYON);
  sleeping_ = false;
  lastDrawMs_ = 0; // force immediate redraw on next UI tick
}
*/
bool DisplayManager::begin() {
  Wire.begin();          // default ESP32 pins: SDA=21, SCL=22
  Wire.setClock(400000);

  // Try 0x3C first (most common), then 0x3D
  if (!oled_.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!oled_.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      ready_ = false;
      Serial.println("[OLED] begin failed (check wiring/address 0x3C/0x3D)");
      return false;
    }
  }

  ready_ = true;

  oled_.clearDisplay();
  oled_.setTextColor(SSD1306_WHITE);
  oled_.setTextSize(1);
  oled_.setCursor(0, 0);
  oled_.println("OLED OK");
  oled_.display();
  sleeping_ = false;

  return true;
}

void DisplayManager::sleep() {
  if (!ready_ || sleeping_) return;
  oled_.ssd1306_command(SSD1306_DISPLAYOFF);
  sleeping_ = true;
}

void DisplayManager::wake() {
  if (!ready_ || !sleeping_) return;
  oled_.ssd1306_command(SSD1306_DISPLAYON);
  sleeping_ = false;
  lastDrawMs_ = 0; // force immediate redraw on next UI tick
}


void DisplayManager::drawHeader_(const String& title) {
  oled_.setCursor(0, 0);
  oled_.print(title);
  oled_.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void DisplayManager::showBoot(const String& line) {
  if (sleeping_) wake();
  // keep boot UI very simple; no heavy refresh logic needed
  oled_.clearDisplay();
  drawHeader_("ESP32");
  oled_.setCursor(0, 16);
  oled_.print(line);
  oled_.display();
}

void DisplayManager::showStatus(const AppState& state, const WifiManager& wifi) {
  if (sleeping_) return;
  // throttle drawing
  uint32_t now = millis();
  if (now - lastDrawMs_ < 250) return;
  lastDrawMs_ = now;

  oled_.clearDisplay();

  if (state.mode == AppMode::AP_SETUP) {
    drawHeader_("SETUP (AP)");
    oled_.setCursor(0, 16);
    oled_.print("SSID: ESP32-SETUP");
    oled_.setCursor(0, 28);
    oled_.print("Open: 192.168.4.1");
    oled_.setCursor(0, 40);
    oled_.print("IP: ");
    oled_.print(wifi.ipString());
  } else {
    drawHeader_("NORMAL (STA)");
    oled_.setCursor(0, 16);
    oled_.print(state.wifiConnected ? "WiFi: Connected" : "WiFi: Connecting");
    oled_.setCursor(0, 28);
    oled_.print("Host: ");
    oled_.print(state.hostname);
    oled_.setCursor(0, 40);
    oled_.print("IP: ");
    oled_.print(state.ip);
    oled_.setCursor(0, 52);
    oled_.print("http://");
    oled_.print(state.hostname);
    oled_.print(".local");
  }

  oled_.display();
}


void DisplayManager::drawWifiIcon_(int16_t x, int16_t y, bool ok) {
  // Lean vertical signal-bars icon (matches reference better than wide arcs).
  const int16_t baseY = y + 10;
  oled_.drawLine(x + 2, baseY, x + 2, baseY - 1, SSD1306_WHITE);  // bar 1
  oled_.drawLine(x + 4, baseY, x + 4, baseY - 3, SSD1306_WHITE);  // bar 2
  oled_.drawLine(x + 6, baseY, x + 6, baseY - 5, SSD1306_WHITE);  // bar 3
  oled_.drawLine(x + 8, baseY, x + 8, baseY - 7, SSD1306_WHITE);  // bar 4
  if (!ok) {
    oled_.drawLine(x + 1, y + 11, x + 10, y + 2, SSD1306_WHITE);
  }
}

void DisplayManager::drawInternetIcon_(int16_t x, int16_t y, bool ok) {
  // Small globe
  const int16_t cx = x + 6;
  const int16_t cy = y + 6;
  oled_.drawCircle(cx, cy, 5, SSD1306_WHITE);
  oled_.drawLine(cx, cy - 5, cx, cy + 5, SSD1306_WHITE);
  oled_.drawLine(cx - 4, cy, cx + 4, cy, SSD1306_WHITE);
  oled_.drawLine(cx - 3, cy - 3, cx + 3, cy - 3, SSD1306_WHITE);
  oled_.drawLine(cx - 3, cy + 3, cx + 3, cy + 3, SSD1306_WHITE);
  if (!ok) {
    oled_.drawLine(x + 1, y + 11, x + 11, y + 1, SSD1306_WHITE);
  }
}

void DisplayManager::drawScaleIcon_(int16_t x, int16_t y, bool ok) {
  // Kettlebell/weight silhouette (clearer for load/scale than balance-scale icon)
  oled_.drawLine(x + 4, y + 2, x + 8, y + 2, SSD1306_WHITE);   // handle top
  oled_.drawLine(x + 3, y + 3, x + 4, y + 2, SSD1306_WHITE);
  oled_.drawLine(x + 8, y + 2, x + 9, y + 3, SSD1306_WHITE);
  oled_.drawRect(x + 2, y + 4, 9, 6, SSD1306_WHITE);           // body
  oled_.drawPixel(x + 2, y + 4, SSD1306_BLACK);                 // soften corners
  oled_.drawPixel(x + 10, y + 4, SSD1306_BLACK);
  if (!ok) {
    oled_.drawLine(x + 1, y + 11, x + 11, y + 1, SSD1306_WHITE);
  }
}

void DisplayManager::drawCommsArrows_(int16_t x, int16_t y, bool txActive, bool rxActive) {
  // Two compact arrows on the top-right: up (tx) and down (rx)
  // Up arrow (left)
  if (txActive) {
    oled_.drawLine(x + 2, y + 10, x + 2, y + 3, SSD1306_WHITE);
    oled_.drawLine(x + 2, y + 3, x,     y + 5, SSD1306_WHITE);
    oled_.drawLine(x + 2, y + 3, x + 4, y + 5, SSD1306_WHITE);
  } else {
    oled_.drawPixel(x + 2, y + 10, SSD1306_WHITE);
    oled_.drawPixel(x + 2, y + 8, SSD1306_WHITE);
    oled_.drawPixel(x + 2, y + 6, SSD1306_WHITE);
  }
  // Down arrow (right)
  if (rxActive) {
    oled_.drawLine(x + 10, y + 2, x + 10, y + 9, SSD1306_WHITE);
    oled_.drawLine(x + 10, y + 9, x + 8,  y + 7, SSD1306_WHITE);
    oled_.drawLine(x + 10, y + 9, x + 12, y + 7, SSD1306_WHITE);
  } else {
    oled_.drawPixel(x + 10, y + 2, SSD1306_WHITE);
    oled_.drawPixel(x + 10, y + 4, SSD1306_WHITE);
    oled_.drawPixel(x + 10, y + 6, SSD1306_WHITE);
  }
}

// ---------------------------------------------------------------------------
// Combined Normal-mode screen:
//   Yellow zone (y=0..15)  — three status icons
//   Blue zone   (y=16..63) — scale weight reading
// ---------------------------------------------------------------------------
void DisplayManager::showMainNormal(const AppState& state, const String& weight, bool scaleActive) {
  if (sleeping_) return;
  uint32_t now = millis();
  if (now - lastDrawMs_ < 200) return;
  lastDrawMs_ = now;

  oled_.clearDisplay();

  // --- Top strip: compact status icons on left, comm arrows on right ---
  const uint32_t nowMs = millis();
  const bool txActive = (state.commTxMs != 0 && (uint32_t)(nowMs - state.commTxMs) < 350);
  const bool rxActive = (state.commRxMs != 0 && (uint32_t)(nowMs - state.commRxMs) < 350);
  drawWifiIcon_(1, 2, state.wifiConnected);      // WiFi link
  drawInternetIcon_(15, 2, state.timeSynced);    // internet proxy (NTP sync)
  // drawScaleIcon_(29, 2, scaleActive);            // RS232 scale stream
  drawCommsArrows_(113, 1, txActive, rxActive);  // HTTP+MQTT activity indicator

  // Top-strip IP (centered between left status icons and right comm arrows).
  oled_.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  oled_.getTextBounds(state.ip, 0, 0, &x1, &y1, &w, &h);
  const int16_t ipMinX = 0;               // nudge IP block a bit right
  const int16_t ipMaxX = 110 - (int16_t)w; // keep breathing room before comm arrows
  int16_t ipX = (128 - (int16_t)w) / 2;
  if (ipX < ipMinX) ipX = ipMinX;
  if (ipX > ipMaxX) ipX = ipMaxX;
  if (ipX < 0) ipX = 0;
  oled_.setCursor(ipX, 4);
  oled_.print(state.ip);
  //oled_.print("192.168.10.70");

  // --- Blue zone: scale reading ---
  if (!scaleActive) {
    oled_.setTextSize(2);
    oled_.setCursor(2, 22);
    oled_.print("NO SCALE");
    oled_.setTextSize(1);
    oled_.setCursor(10, 44);
    oled_.print("Check RS232 / baud");
  } else {
    // Auto-scale by rendered width so larger values still fit (e.g. -xxxx.x).
    // Keep a conservative max size to avoid right-edge clipping on longer values.
    uint8_t weightSize = 3;
    uint16_t ww = 0, wh = 0;
    int16_t wx = 0, wy = 0;
    while (weightSize > 1) {
      oled_.setTextSize(weightSize);
      oled_.getTextBounds(weight, 0, 0, &wx, &wy, &ww, &wh);
      if (ww <= 122) break; // 128px width with small side margins
      weightSize--;
    }
    // One line lower than before to improve balance under top strip.
    int16_t weightY = (weightSize == 3) ? 30 : 34;
    oled_.setTextSize(weightSize);
    oled_.setCursor(2, weightY);
    oled_.print(weight);
  }

  oled_.display();
}

/*
void DisplayManager::showQrUrl(const String& url) {
  oled_.clearDisplay();

  // QR version 3 fits "http://192.168.x.x/"
  constexpr uint8_t QR_VERSION = 3;
  uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
  QRCode qrcode;

  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, url.c_str());

  const int qrSize = qrcode.size; // modules per side

  const int maxW = 128;
  const int maxH = 64;

  int scale = 1;
  while ((qrSize * (scale + 1)) <= maxW &&
         (qrSize * (scale + 1)) <= maxH) {
    scale++;
  }

  const int drawW = qrSize * scale;
  const int drawH = qrSize * scale;
  const int x0 = (maxW - drawW) / 2;
  const int y0 = (maxH - drawH) / 2;

  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        oled_.fillRect(
          x0 + x * scale,
          y0 + y * scale,
          scale,
          scale,
          SSD1306_WHITE
        );
      }
    }
  }

  oled_.display();
}

void DisplayManager::showQrUrl(const String& url) {
  if (!ready_) {
    Serial.println("[OLED] Not ready, cannot show QR");
    return;
  }
  if (url.length() == 0) return;

  // Fixed buffer (avoids "not constant" compile error)
  // 256 bytes is enough for QR version 3/4 in this library.
  static uint8_t qrcodeData[256];
  QRCode qrcode;

  constexpr uint8_t QR_VERSION = 3; // good for "http://192.168.x.x/"
  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, url.c_str());

  oled_.clearDisplay();

  const int qrSize = qrcode.size;
  const int maxW = 128;
  const int maxH = 64;

  int scale = 1;
  while ((qrSize * (scale + 1)) <= maxW && (qrSize * (scale + 1)) <= maxH) {
    scale++;
  }

  const int drawW = qrSize * scale;
  const int drawH = qrSize * scale;
  const int x0 = (maxW - drawW) / 2;
  const int y0 = (maxH - drawH) / 2;

  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        oled_.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, SSD1306_WHITE);
      }
    }
  }

  oled_.display();
}

*/
void DisplayManager::showQrUrl(const String& url) {
  oled_.clearDisplay();

  // Version 2 fits typical "http://192.168.1.123" URLs reliably.
  constexpr uint8_t QR_VERSION = 2;
  uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
  QRCode qrcode;

  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, url.c_str());
  const int qr = qrcode.size; // 25 modules for v2

  // IMPORTANT for 128x64:
  // 4-module quiet zone makes scale drop to 1 => unreadable.
  // 3-module quiet zone still scans well on OLED in practice.
  const int quiet = 3;
  const int total = qr + 2 * quiet; // 25 + 6 = 31

  const int maxW = 128;
  const int maxH = 64;

  int scale = min(maxW / total, maxH / total); // for 64/31 => 2

  // If something still makes it too small, show text.
  if (scale < 2) {
    oled_.setTextSize(1);
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setCursor(0, 0);
    oled_.println("Open in browser:");
    oled_.println(url);
    oled_.display();
    return;
  }

  // Center including quiet zone
  const int drawW = total * scale;
  const int drawH = total * scale;
  const int x0 = (maxW - drawW) / 2;
  const int y0 = (maxH - drawH) / 2;

  // Camera-friendly: black modules on bright background
  oled_.fillRect(x0, y0, drawW, drawH, SSD1306_WHITE);

  for (int y = 0; y < qr; y++) {
    for (int x = 0; x < qr; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        oled_.fillRect(
          x0 + (x + quiet) * scale,
          y0 + (y + quiet) * scale,
          scale, scale,
          SSD1306_BLACK
        );
      }
    }
  }

  oled_.display();
}
