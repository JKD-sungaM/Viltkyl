#include <Arduino.h>

constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr unsigned long BLINK_MS = 500;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  delay(1200);
  Serial.println("Viltkyl: Nano ESP32 ansluten via USB-C");
  Serial.println("Init klar. Blinktest startar.");
}

void loop() {
  static unsigned long lastToggle = 0;
  static bool ledState = false;

  unsigned long now = millis();
  if (now - lastToggle >= BLINK_MS) {
    lastToggle = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    Serial.println(ledState ? "LED: ON" : "LED: OFF");
  }
}
