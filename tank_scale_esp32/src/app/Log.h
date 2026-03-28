#pragma once
#include <Arduino.h>

class Log {
public:
  static void begin(uint32_t baud);

  static void i(const char* msg);
  static void w(const char* msg);
  static void e(const char* msg);

  static void i(const String& msg);
  static void w(const String& msg);
  static void e(const String& msg);
};
