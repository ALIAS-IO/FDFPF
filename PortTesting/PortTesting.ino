const int analogPin = D0;  // use a voltage divider to bring 5V down to 3.3V safe range

void setup() {
  Serial.begin(115200);
}

void loop() {
  int raw = analogRead(analogPin);
  float voltage = raw * (3.3 / 4095.0);  // ESP32-C3 is 12-bit ADC
  Serial.print("Voltage on pin: ");
  Serial.println(voltage);
  delay(500);
}