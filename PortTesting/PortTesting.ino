#include <Arduino.h>

// List the pins you want to test
const byte testPins[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
const byte numPins = sizeof(testPins);

bool lastState[numPins];

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("=== PORT BUTTON TESTER ===");

  for (byte i = 0; i < numPins; i++) {
    pinMode(testPins[i], INPUT_PULLUP);
    lastState[i] = digitalRead(testPins[i]);
  }
}

void loop() {
  for (byte i = 0; i < numPins; i++) {
    bool currentState = digitalRead(testPins[i]);

    // Detect state change
    if (currentState != lastState[i]) {
      delay(10); // simple debounce

      currentState = digitalRead(testPins[i]);

      if (currentState != lastState[i]) {

        if (currentState == LOW) {
          Serial.print("Button PRESSED on pin ");
          Serial.println(testPins[i]);
        } else {
          Serial.print("Button RELEASED on pin ");
          Serial.println(testPins[i]);
        }

        lastState[i] = currentState;
      }
    }
  }
}