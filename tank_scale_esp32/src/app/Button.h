#pragma once
#include <Arduino.h>

class Button {
public:
  void begin(uint8_t pin, bool pullup, bool activeLow);
  void update();

  bool isPressed() const { return pressed_; }

private:
  uint8_t pin_ = 255;
  bool activeLow_ = true;

  bool pressed_ = false;
};
