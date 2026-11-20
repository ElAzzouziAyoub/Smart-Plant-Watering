// =====================================================================
//  WATER LEVEL SENSOR TEST  (upload this alone, then open Serial 9600)
//
//  Wiring (same as the main sketch):
//    water level sensor  S -> A1,  + -> A2,  - -> GND
//    moisture sensor     S -> A0,  + -> D7,  - -> GND
//
//  Every second it prints A0, A1, A3 twice:
//    - once with the probes unpowered (D7 + A2 LOW)  -> should be ~0
//    - once with the probes powered   (D7 + A2 HIGH) -> the real readings
//
//  How to read the result:
//   * A1 (POWERED line) rises when you dip the sensor -> sensor + wiring OK
//   * A3 changes instead of A1                       -> S wire is on A3
//   * A1 stays ~0 even when dipped                   -> + not on A2, GND or S
//                                                      not connected, or the
//                                                      sensor is dead
//
//  All relays stay OFF during this test.
// =====================================================================
#include <Arduino.h>

const byte MOISTURE_POWER = 7;
const byte WATER_POWER    = A2;
const byte RELAYS[] = { 8, 9, 10 };   // fan, heater, pump: keep OFF

int readStable(byte pin) {
  analogRead(pin);                    // first read after switching pin: discard
  return analogRead(pin);
}

void printPins(const __FlashStringHelper* label) {
  Serial.print(label);
  Serial.print(F("  A0(moisture)=")); Serial.print(readStable(A0));
  Serial.print(F("  A1(water)="));    Serial.print(readStable(A1));
  Serial.print(F("  A3="));           Serial.println(readStable(A3));
}

void power(bool on) {
  digitalWrite(MOISTURE_POWER, on ? HIGH : LOW);
  digitalWrite(WATER_POWER,    on ? HIGH : LOW);
}

void setup() {
  Serial.begin(9600);
  for (byte r : RELAYS) { pinMode(r, OUTPUT); digitalWrite(r, LOW); }
  pinMode(MOISTURE_POWER, OUTPUT);
  pinMode(WATER_POWER, OUTPUT);
  power(false);
  Serial.println(F("Water level test - dip the sensor and watch A1 (POWERED line)"));
}

void loop() {
  power(false);
  delay(100);
  printPins(F("unpowered:"));

  power(true);
  delay(100);
  printPins(F("POWERED  :"));
  power(false);

  Serial.println();
  delay(1000);
}
