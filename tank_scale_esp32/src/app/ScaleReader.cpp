#include "ScaleReader.h"

void ScaleReader::begin(HardwareSerial& port, int rxPin, int txPin, uint32_t baud) {
  port_ = &port;

  // T7E baud is configurable; 9600 is common.
  // Most Yaohua docs describe ASCII output; parity isn't always stated,
  // so we default to 8N1 (works with many units). If your unit is set
  // to even parity, change SERIAL_8N1 -> SERIAL_8E1.
  port_->begin(baud, SERIAL_8N1, rxPin, txPin);

  line_.reserve(64);
  lastRaw_.reserve(32);
  lastPretty_.reserve(32);
}

void ScaleReader::loop() {
  if (!port_) return;

  while (port_->available()) {
    char c = (char)port_->read();

    // Start of frame
    if (c == '=') {
      inFrame_ = true;
      frameLen_ = 0;
      frame_[frameLen_++] = c;
      continue;
    }

    if (!inFrame_) continue;

    // Keep only printable ASCII (drops the garbage if baud/noise happens)
    if (c < 32 || c > 126) {
      continue;
    }

    if (frameLen_ < sizeof(frame_) - 1) {
      frame_[frameLen_++] = c;
    }

    // T7E frames you’re seeing are 8 chars total (ex: "=0.0000-")
    if (frameLen_ >= 8 || frameLen_ >= 10) // safer: use a delimiter approach
     {
      frame_[frameLen_] = '\0';
      String s(frame_);
      inFrame_ = false;

      s.trim();
      if (s.length()) {
        handleLine_(s);
        if (debug_) {
          debug_->print("FRAME: ");
          debug_->println(s);
        }
      }
    }
  }
}

/*
void ScaleReader::handleLine_(String s) {
  lastRaw_ = s;

  // Typical frames start with '='; tolerate extra spaces.
  int eq = s.indexOf('=');
  if (eq >= 0) {
    s = s.substring(eq + 1);
  }
  s.trim();

  lastPretty_ = decodeT7E_(s);
  hasReading_ = true;
}


void ScaleReader::handleLine_(String s) {
  lastRaw_ = s;

  // remove '='
  if (s.length() && s[0] == '=') s = s.substring(1);
  s.trim();

  lastPretty_ = decodeT7E_(s);
  hasReading_ = true;

  lastRxMs_ = millis();

  // If NTP time is valid, store epoch time too
  time_t now = time(nullptr);
  if (now > 1609459200) { // 2021-01-01
    lastRxEpoch_ = now;
  }



}


String ScaleReader::decodeT7E_(const String& payload) {
  // Remove spaces.
  String p;
  p.reserve(payload.length());
  for (size_t i = 0; i < payload.length(); i++) {
    char c = payload[i];
    if (c != ' ') p += c;
  }
  p.trim();
  if (p.length() < 2) return payload;

  // Sign is last character: '-' negative, '0' positive.
  const char signChar = p[p.length() - 1];
  const bool negative = (signChar == '-');
  String body = p.substring(0, p.length() - 1);

  // The manual states "lower digits in front" => reverse the body.
  String normal;
  normal.reserve(body.length() + 2);
  for (int i = (int)body.length() - 1; i >= 0; i--) {
    normal += body[(size_t)i];
  }
  normal.trim();

  if (negative) normal = "-" + normal;
  return normal;
}


String ScaleReader::decodeT7E_(const String& payload) {
  // Remove spaces
  String p;
  p.reserve(payload.length());
  for (size_t i = 0; i < payload.length(); i++) {
    char c = payload[i];
    if (c != ' ') p += c;
  }
  p.trim();
  if (p.length() < 2) return payload;

  // last char is sign: '-' negative, '0' positive
  char signChar = p[p.length() - 1];
  bool negative = (signChar == '-');
  if (!(signChar == '-' || signChar == '0')) {
    return payload; // unexpected
  }

  String body = p.substring(0, p.length() - 1);
  body.trim();

  String normal = body;

  // Manual says digits may be reversed; your observed frames look normal.
  // Flip this with setReverseDigits(true) if needed.
  if (reverseDigits_) {
    normal = "";
    normal.reserve(body.length());
    for (int i = (int)body.length() - 1; i >= 0; --i) normal += body[(size_t)i];
    normal.trim();
  }

  if (negative) normal = "-" + normal;
  return normal;
}
*/
String ScaleReader::compactSigned_(const String& normalized) const {
  if (normalized.length() < 2) return normalized;

  char sign = normalized[0];          // '+' or '-'
  if (!(sign == '+' || sign == '-')) {
    // If something unexpected got stored, just return it
    return normalized;
  }

  String v = normalized.substring(1); // value part
  v.trim();
  if (v.length() == 0) return String(sign) + "0";

  // Remove leading zeros (but keep "0.xxx")
  int dot = v.indexOf('.');
  if (dot >= 0) {
    int i = 0;
    while (i < dot - 1 && v[i] == '0') i++;
    v = v.substring(i);
    if (v.startsWith(".")) v = "0" + v;
  } else {
    int i = 0;
    while (i < (int)v.length() - 1 && v[i] == '0') i++;
    v = v.substring(i);
  }

  // Trim trailing zeros after decimal
  int dot2 = v.indexOf('.');
  if (dot2 >= 0) {
    while (v.endsWith("0")) v.remove(v.length() - 1);
    if (v.endsWith(".")) v.remove(v.length() - 1);
    if (v.length() == 0) v = "0";
  }

  return String(sign) + v;
}


String ScaleReader::lastDisplayWeight() const {
  if (!hasReading_) return "--";
  return compactSigned_(lastNormalized_);
}

bool ScaleReader::lastWeightValue(float& out) const {
  if (!hasReading_) return false;
  const String& s = lastPretty_; // normalized signed string like +123.45
  if (!s.length()) return false;

  char* endp = nullptr;
  float v = strtof(s.c_str(), &endp);
  if (endp == s.c_str()) return false;

  out = v;
  return true;
}


String ScaleReader::hhmmss_(time_t t) const {
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(buf);
}

String ScaleReader::lastTimestampString() const {
  if (!hasReading_) return "No data";

  // Prefer real clock time if NTP is valid
  if (lastRxEpoch_ > 1609459200) {
    return hhmmss_(lastRxEpoch_);
  }

  // Otherwise show relative age
  uint32_t ageMs = millis() - lastRxMs_;
  uint32_t s = ageMs / 1000;
  if (s < 60) return String(s) + "s ago";
  return String(s / 60) + "m ago";
}

void ScaleReader::handleLine_(String s) {
  lastRaw_ = s;

  // remove '='
  if (s.length() && s[0] == '=') s = s.substring(1);
  s.trim();

  // Parse candidate; ignore malformed frames and keep last valid reading.
  const String parsed = decodeT7E_(s);
  if (parsed.length() == 0) {
    return;
  }

  // Save normalized signed string (always starts with + or -)
  lastNormalized_ = parsed;
  lastPretty_ = lastNormalized_; // keep existing API behavior
  hasReading_ = true;
  lastRxMs_ = millis();

  // If NTP time is valid, store epoch time too.
  time_t now = time(nullptr);
  if (now > 1609459200) { // 2021-01-01
    lastRxEpoch_ = now;
  }
}

String ScaleReader::decodeT7E_(const String& payload) {
  // Remove spaces
  String p;
  p.reserve(payload.length());
  for (size_t i = 0; i < payload.length(); i++) {
    char c = payload[i];
    if (c != ' ') p += c;
  }
  p.trim();
  if (p.length() < 2) return "";

  // last char is sign: '-' negative, '0' positive
  char signChar = p[p.length() - 1];
  bool negative = (signChar == '-');
  if (!(signChar == '-' || signChar == '0')) {
    return ""; // unexpected
  }

  String body = p.substring(0, p.length() - 1);
  body.trim();
  if (body.length() == 0) return "";

  String normal = body;

  // Optional reversal (keep as you had it)
  if (reverseDigits_) {
    normal = "";
    normal.reserve(body.length());
    for (int i = (int)body.length() - 1; i >= 0; --i) normal += body[(size_t)i];
    normal.trim();
  }

  // Ensure there's at least one digit
  if (normal.length() == 0) return "";

  // Always include explicit sign
  return String(negative ? '-' : '+') + normal;
}
