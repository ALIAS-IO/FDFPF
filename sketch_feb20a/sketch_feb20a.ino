#include <Wire.h>

void setup() {
  Wire.begin(4, 5);
  Serial.begin(115200);
  Serial.println("Scanning I2C addresses...");
  
  for (byte addr = 0x20; addr <= 0x3F; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found I2C device at 0x");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("Scan done.");
}

void loop() {}