#include "Log.h"

void Log::begin(uint32_t baud) {
  Serial.begin(baud);
  delay(50);
}

static void printTag(const char* tag, const String& msg) {
  Serial.print('['); Serial.print(tag); Serial.print("] ");
  Serial.println(msg);
}

void Log::i(const char* msg) { printTag("I", String(msg)); }
void Log::w(const char* msg) { printTag("W", String(msg)); }
void Log::e(const char* msg) { printTag("E", String(msg)); }

void Log::i(const String& msg) { printTag("I", msg); }
void Log::w(const String& msg) { printTag("W", msg); }
void Log::e(const String& msg) { printTag("E", msg); }
