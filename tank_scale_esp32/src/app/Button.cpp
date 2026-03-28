#include "Button.h"

void Button::begin(uint8_t pin, bool pullup, bool activeLow) {
  pin_ = pin;
  activeLow_ = activeLow;
  pinMode(pin_, pullup ? INPUT_PULLUP : INPUT);
  update();
}

void Button::update() {
  if (pin_ == 255) return;
  int v = digitalRead(pin_);
  pressed_ = activeLow_ ? (v == LOW) : (v == HIGH);
}
