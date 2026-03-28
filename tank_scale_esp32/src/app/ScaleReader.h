#pragma once

#include <Arduino.h>

// Reads Yaohua T7E RS232 frames (through a MAX3232 level shifter) from a UART.
//
// Default data format (from the T7E manual):
//   "= <weightdata>" where <weightdata> is 6 digits incl. decimal point,
//   but transmitted *low digits first* and the sign is the last character
//   ('0' for positive, '-' for negative).
// Example: 500.00kg => "= 00.0050" ; -500.00kg => "= 00.005-".
//
// We keep responsibilities separated:
//  - ScaleReader: serial + parsing + latest reading
//  - DisplayManager: rendering
//  - App: UI state machine + routing
class ScaleReader {
public:
  void begin(HardwareSerial& port, int rxPin, int txPin, uint32_t baud);
  void loop();

  bool hasReading() const { return hasReading_; }
  const String& lastRaw() const { return lastRaw_; }
  const String& lastPretty() const { return lastPretty_; }

  void setDebug(Stream* s) { debug_ = s; }
  void setReverseDigits(bool v) { reverseDigits_ = v; }

  String lastDisplayWeight() const;     // "+7.2", "-0.5", "+12.34"
  String lastTimestampString() const;   // "14:23:08" or "12s ago"
  uint32_t lastRxMs() const { return lastRxMs_; }
  bool lastWeightValue(float& out) const;

private:
  HardwareSerial* port_ = nullptr;
  String line_;

  bool hasReading_ = false;
  String lastRaw_;
  String lastPretty_;

  void handleLine_(String s);
  String decodeT7E_(const String& payload);

  Stream* debug_ = nullptr;
  bool reverseDigits_ = false;     // set true if you confirm digits are reversed
  bool inFrame_ = false;
  char frame_[16];
  uint8_t frameLen_ = 0;

  uint32_t lastRxMs_ = 0;
  time_t lastRxEpoch_ = 0;   // 0 if unknown
  String lastNormalized_;    // e.g. "-7.2000" or "+1.0000"
  
  String compactSigned_(const String& normalized) const;
  String hhmmss_(time_t t) const;


}; 
