#include <Arduino.h>
#include "app/App.h"

App app;
/*
void debugRs232Raw() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    Serial.print("0x");
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);
    Serial.print(" ");

    if (b >= 32 && b <= 126) {
      Serial.print("('");
      Serial.print((char)b);
      Serial.print("')");
    }

    Serial.println();
  }
}

void debugT7EFrames() {
  static char buf[32];
  static int idx = 0;

  while (Serial2.available()) {
    char c = (char)Serial2.read();

    // Start of frame
    if (c == '=') {
      idx = 0;
      buf[idx++] = c;
      continue;
    }

    // Only build frame after we've seen '='
    if (idx > 0) {
      // Keep printable chars only (helps spot baud issues)
      if (c >= 32 && c <= 126) {
        if (idx < (int)sizeof(buf) - 1) buf[idx++] = c;
      } else {
        // If we see non-printables inside a frame, dump diagnostic
        Serial.print("NONPRINT in frame: 0x");
        Serial.println((uint8_t)c, HEX);
      }

      // Manual says: '=' + 7 chars (6 digits incl '.' + sign char) → usually 8 total
      if (idx >= 8) {
        buf[idx] = '\0';
        Serial.print("FRAME: ");
        Serial.println(buf);
        idx = 0;
      }
    }
  }
}
*/

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32 booting...");

  constexpr int RS232_RX = 16;
  constexpr int RS232_TX = 17;   // not used yet
  constexpr int RS232_BAUD = 9600; // try 600 later if needed

  Serial2.begin(RS232_BAUD, SERIAL_8N1, RS232_RX, RS232_TX);
  Serial.println("RS232 Serial2 started");


  app.begin();
}

void loop() {
  app.loop();
}

