#include <Arduino.h>
#include <avr/wdt.h>     // watchdog: resets the board if the program ever freezes
#include <MAX6675.h>     // Rob Tillaart's MAX6675 library (Library Manager: "MAX6675")
#include "Automaton.h"
#include "Settings.h"   // the thresholds (editable from the screen, saved in EEPROM)
#include "Menu.h"       // LCD screen + 5 buttons

// =====================================================================
//  SMART GREENHOUSE
//   1. thresholds: defaults in Settings.cpp, changed from the screen menu
//   2. two automata model the controller: irrigation and temperature
//   3. loop(): read the sensors -> each automaton takes its transition
//              -> the relays follow the current states
// =====================================================================

// ---------------------------------------------------------------------
// PINS
// ---------------------------------------------------------------------
#define MOISTURE_SENSOR        A0   // analog: moisture sensor output
#define WATER_LEVEL_SENSOR     A1   // analog: water level in the tank
#define MOISTURE_SENSOR_POWER  7    // digital: powers the sensor only while reading
                                    //          (stops a resistive probe from corroding)
#define WATER_LEVEL_POWER      A2   // used as a digital output: powers the water level
                                    //          sensor only while reading (same reason)
// Thermocouple (MAX6675 module)
#define TEMPERATURE_SENSOR_SCK 13
#define TEMPERATURE_SENSOR_CS  12
#define TEMPERATURE_SENSOR_SO  11

// Actuators (relays)
#define PUMP_ACTUATOR          10
#define FAN_ACTUATOR           8
#define HEATER_ACTUATOR        9

// Some relay modules turn ON with LOW instead of HIGH.
// If an actuator runs when it should be off, swap its two lines.
#define PUMP_ON    HIGH
#define PUMP_OFF   LOW
#define FAN_ON     HIGH
#define FAN_OFF    LOW
#define HEATER_ON  HIGH
#define HEATER_OFF LOW

// ---------------------------------------------------------------------
// THRESHOLDS + CALIBRATION: see Settings.h / Settings.cpp (global 'settings')
// ---------------------------------------------------------------------
const unsigned long SAMPLE_MS = 2000UL;  // how often the sensors are read

// Moisture calibration: raw A0 reading -> %   (0 % = dry, 100 % = in water)
// Read "raw" in the Serial Monitor with the probe in dry air, then in a glass of water.
const int MOISTURE_RAW_DRY = 850;  // raw in dry air      <-- measure
const int MOISTURE_RAW_WET = 140;   // raw in water        <-- measure

// Tank-level protection is always enabled.
// H_TARGET = 0 disables automatic watering (including the pump output).

// Initial cooldown: after a reset, NOTHING starts for 30 s.
// Sensors, Serial and the LCD menu already work, but both automata stay in
// IDLE and every actuator stays OFF until the delay is over.
// (the normal COOLDOWN_MS still applies between two waterings).
const unsigned long STARTUP_DELAY_MS = 30000UL;
unsigned long startupTime = 0;     // millis() at the end of setup()
bool automataStarted = false;


// =====================================================================
//  SENSORS
// =====================================================================
// Latest measurements (updated every SAMPLE_MS).
int   moistureRaw = 0;   // 0..1023, higher = drier soil (kept for calibration)
int   moisture    = 0;   // %, 0 = dry .. 100 = wet  (used by the automaton)
int   waterRaw    = 0;   // original A1 reading, 0..1023
bool  tankOk = false;     // enough water in the tank?
float T = NAN;           // temperature in C (NAN = no valid reading)

// Constructor order for this library: (CS, SO, SCK)
MAX6675 thermocouple(TEMPERATURE_SENSOR_CS, TEMPERATURE_SENSOR_SO, TEMPERATURE_SENSOR_SCK);

// analogRead() twice, keep the second: after switching from A0 to A1 the
// ADC needs one conversion to settle (the first value can be wrong).
int analogReadStable(byte pin) {
  analogRead(pin);
  return analogRead(pin);
}

// Both probes are powered only while they are read (D7 + A2 HIGH),
// so their metal tracks don't corrode from constant current.
void powerProbes(bool on) {
  digitalWrite(MOISTURE_SENSOR_POWER, on ? HIGH : LOW);
  digitalWrite(WATER_LEVEL_POWER,     on ? HIGH : LOW);
}

// Read moisture (A0) and water level (A1). Call it while the probes are powered.
void readProbes(int& moisture, int& water) {
  moisture = analogReadStable(MOISTURE_SENSOR);
  water    = analogReadStable(WATER_LEVEL_SENSOR);
}

// Temperature in C, or NAN if something is wrong.
// Don't call it more often than every 250 ms (the chip needs time to measure).
float readTemperature() {
  if (thermocouple.read() != STATUS_OK) return NAN;
  // Raw data 0x0000 = SO stuck LOW: the library would say "0.00 C", treat it as an error.
  if (thermocouple.getRawData() == 0) return NAN;
  return thermocouple.getCelsius();
}

// Raw reading -> moisture %, rounded and kept inside 0..100.
int moisturePercent(int raw) {
  long span = (long)MOISTURE_RAW_DRY - MOISTURE_RAW_WET;
  long pct  = ((long)MOISTURE_RAW_DRY - raw) * 100L;
  pct = (pct + (pct >= 0 ? span / 2 : -span / 2)) / span;
  return constrain(pct, 0, 100);
}

// Stores one complete set of measurements.
void storeSample(int rawMoisture, int rawWater) {
  moistureRaw = rawMoisture;
  moisture = moisturePercent(rawMoisture);
  waterRaw = rawWater;
  tankOk = waterRaw > settings.WATER_LEVEL_MIN;
  T = readTemperature();
}

// Non-blocking version used in loop(): power the probes, keep running
// (buttons and automata stay responsive), read it SENSOR_SETTLE_MS later.
// Returns true when a new sample has just been completed.
const unsigned long SENSOR_SETTLE_MS = 100;
unsigned long lastSample = 0;      // when the last sample was started
bool probePowered = false;

bool updateSensors() {
  unsigned long now = millis();
  if (!probePowered && now - lastSample >= SAMPLE_MS) {
    powerProbes(true);
    probePowered = true;
    lastSample = now;
  }
  if (probePowered && now - lastSample >= SENSOR_SETTLE_MS) {
    int moisture, water;
    readProbes(moisture, water);              // both read while powered
    powerProbes(false);
    probePowered = false;
    storeSample(moisture, water);
    return true;
  }
  return false;
}


// =====================================================================
//  AUTOMATON 1: IRRIGATION
//
//   IDLE ----[H < Htarget and cooldown over and tank ok] / startPump --> IRRIGATING
//   IDLE ----[tank empty]                                           --> TANK_EMPTY
//   IRRIGATING --[tank empty]           / stopPump --> TANK_EMPTY
//   IRRIGATING --[pump ran PUMP_MAX_MS] / stopPump --> IDLE
//   IRRIGATING --[H > Htarget]          / stopPump --> IDLE
//   TANK_EMPTY --[tank ok again]                   --> IDLE
//
//  Output: pump ON exactly in IRRIGATING.
//  Time is NOT inside the automaton: the guards compare millis() with the
//  timestamps saved by the actions.
// =====================================================================
enum { IDLE, IRRIGATING, TANK_EMPTY };
const char* const IRRIGATION_NAME[] = { "IDLE", "IRRIGATING", "TANK_EMPTY" };

unsigned long pumpStart = 0;  // when the pump was last switched ON
unsigned long pumpStop  = 0;  // when the pump was last switched OFF

bool tankEmpty()    { return !tankOk; }
bool tankRefilled() { return tankOk; }
bool soilDry()      { return settings.H_TARGET > 0 && moisture < settings.H_TARGET && millis() - pumpStop >= settings.COOLDOWN_MS && tankOk; }
bool soilWet()      { return moisture > settings.H_TARGET; }
bool pumpTooLong()  { return millis() - pumpStart >= settings.PUMP_MAX_MS; }

void startPump() { pumpStart = millis(); }
void stopPump()  { pumpStop  = millis(); }

Automaton irrigation;
bool pumpOn() { return settings.H_TARGET > 0 && tankOk && irrigation.state() == IRRIGATING; }


// =====================================================================
//  AUTOMATON 2: TEMPERATURE
//
//   IDLE ----[T < Tmin]  --> HEATING   (heater + fan ON together)
//   IDLE ----[T >= Tmax] --> COOLING   (fan ON)
//   HEATING --[T >= Tmin]                           --> IDLE
//   COOLING --[T < Tmax and fan ran THERMAL_MIN_ON] --> IDLE
//   any state --[no valid reading] --> SENSOR_ERROR (all OFF)
//   SENSOR_ERROR --[valid reading again] --> IDLE
//
//  No time limit: heating runs until Tmin is reached, cooling until T
//  drops below Tmax. The fan runs during heating to spread the warm air.
// =====================================================================
enum { C_IDLE, C_HEATING, C_COOLING, C_SENSOR_ERROR };
const char* const TEMPERATURE_NAME[] = { "IDLE", "HEATING", "COOLING", "SENSOR_ERROR" };

Automaton temperature;
unsigned long thermalStart = 0;  // when cooling started

bool tempInvalid() { return isnan(T); }
bool tempValid()   { return !isnan(T); }
bool tooCold()     { return T < settings.T_LOW; }
bool tooHot()      { return T >= settings.T_HIGH; }
bool warmEnough()  { return T >= settings.T_LOW; }
bool coolEnough() { return T < settings.T_HIGH && millis() - thermalStart >= settings.THERMAL_MIN_ON_MS; }
void startThermal() { thermalStart = millis(); }

bool heaterOn() { return temperature.state() == C_HEATING; }
bool fanOn()    { return temperature.state() == C_HEATING || temperature.state() == C_COOLING; }


// =====================================================================
//  SERIAL OUTPUT
// =====================================================================
void logTransition(const char* automaton, const char* from, const char* to) {
  Serial.print(F("# ["));
  Serial.print(millis() / 1000.0, 1);       // seconds since reset
  Serial.print(F(" s] ")); Serial.print(automaton); Serial.print(F(": "));
  Serial.print(from); Serial.print(F(" -> ")); Serial.println(to);
}

void printTemperature() {
  if (!isnan(T)) { Serial.print(T); Serial.print(F(" C")); return; }
  if (thermocouple.getStatus() == STATUS_NO_COMMUNICATION) Serial.print(F("NO COMMUNICATION"));
  else if (thermocouple.getRawData() == 0)                 Serial.print(F("ALL ZEROS (wiring/power)"));
  else                                                     Serial.print(F("PROBE NOT CONNECTED"));
}

// One status line, e.g.
// Includes the original tank ADC value and the empty-tank threshold.
void printStatus() {
  Serial.print(F("H: "));            Serial.print(moisture);
  Serial.print(F(" % (raw "));       Serial.print(moistureRaw);
  Serial.print(F(") Htarget "));     Serial.print(settings.H_TARGET);
  Serial.print(F(" | water raw ")); Serial.print(waterRaw);
  Serial.print(F(" min "));         Serial.print(settings.WATER_LEVEL_MIN);
  Serial.print(tankOk ? " (ok)" : " (EMPTY)");
  Serial.print(F(" | T: "));         printTemperature();
  Serial.print(F(" | irrigation ")); Serial.print(IRRIGATION_NAME[irrigation.state()]);
  Serial.print(F(", temperature ")); Serial.print(TEMPERATURE_NAME[temperature.state()]);
  Serial.print(F(" | pump "));   Serial.print(pumpOn() ? "ON" : "OFF");
  Serial.print(F(" heater "));   Serial.print(heaterOn() ? "ON" : "OFF");
  Serial.print(F(" fan "));      Serial.println(fanOn() ? "ON" : "OFF");
}


// =====================================================================
//  SETUP - runs once
// =====================================================================
void setup() {
  // Safety net: if loop() stops running for 4 s (frozen), the board resets
  // itself, and setup() switches every actuator OFF again.
  wdt_enable(WDTO_4S);
  Serial.begin(9600);
  loadSettings();   // saved values from EEPROM (defaults on first start)
  menuBegin();      // LCD + buttons

  pinMode(MOISTURE_SENSOR_POWER, OUTPUT);
  pinMode(WATER_LEVEL_POWER, OUTPUT);
  powerProbes(false);   // both probes unpowered until the first reading
  pinMode(PUMP_ACTUATOR, OUTPUT);
  pinMode(FAN_ACTUATOR, OUTPUT);
  pinMode(HEATER_ACTUATOR, OUTPUT);

  // safety: every actuator OFF at start
  digitalWrite(PUMP_ACTUATOR, PUMP_OFF);
  digitalWrite(FAN_ACTUATOR, FAN_OFF);
  digitalWrite(HEATER_ACTUATOR, HEATER_OFF);

  thermocouple.begin();
  // The library needs a pull-up on SO to notice a missing/unpowered module.
  pinMode(TEMPERATURE_SENSOR_SO, INPUT_PULLUP);
  delay(250);   // the chip needs ~220 ms to make its first measurement

  // --- Automaton 1: irrigation ---
  // Transitions leaving the same state are tried in the order they are added,
  // so safety transitions are added first.
  irrigation.addState(IDLE);
  irrigation.addState(IRRIGATING);
  irrigation.addState(TANK_EMPTY);

  irrigation.addTransition(IDLE,       TANK_EMPTY, tankEmpty,    nullptr);
  irrigation.addTransition(IDLE,       IRRIGATING, soilDry,      startPump);
  irrigation.addTransition(IRRIGATING, TANK_EMPTY, tankEmpty,    stopPump);
  irrigation.addTransition(IRRIGATING, IDLE,       pumpTooLong,  stopPump);
  irrigation.addTransition(IRRIGATING, IDLE,       soilWet,      stopPump);
  irrigation.addTransition(TANK_EMPTY, IDLE,       tankRefilled, nullptr);

  irrigation.setInitial(IDLE);
  irrigation.init();

  // --- Automaton 2: temperature ---
  temperature.addState(C_IDLE);
  temperature.addState(C_HEATING);
  temperature.addState(C_COOLING);
  temperature.addState(C_SENSOR_ERROR);

  // Order per state: safety first.
  temperature.addTransition(C_IDLE,         C_SENSOR_ERROR, tempInvalid, nullptr);
  temperature.addTransition(C_IDLE,         C_HEATING,      tooCold,     startThermal);
  temperature.addTransition(C_IDLE,         C_COOLING,      tooHot,      startThermal);
  temperature.addTransition(C_HEATING,      C_SENSOR_ERROR, tempInvalid, nullptr);
  temperature.addTransition(C_HEATING,      C_COOLING,      tooHot,      startThermal);
  temperature.addTransition(C_HEATING,      C_IDLE,         warmEnough,  nullptr);
  temperature.addTransition(C_COOLING,      C_SENSOR_ERROR, tempInvalid, nullptr);
  temperature.addTransition(C_COOLING,      C_IDLE,         coolEnough,  nullptr);
  temperature.addTransition(C_SENSOR_ERROR, C_IDLE,         tempValid,   nullptr);

  temperature.setInitial(C_IDLE);
  temperature.init();

  // Real measurements before the first decision (blocking is fine in setup).
  {
    int moisture, water;
    powerProbes(true);
    delay(SENSOR_SETTLE_MS);                 // give the sensors time to settle
    readProbes(moisture, water);
    powerProbes(false);
    storeSample(moisture, water);
  }
  lastSample = millis();
  // The initial cooldown replaces the pump cooldown for the first watering:
  // pretend the pump stopped COOLDOWN_MS ago, so soilDry() is not delayed
  // any further once the STARTUP_DELAY_MS wait is over.
  pumpStop = millis() - settings.COOLDOWN_MS;
  startupTime = millis();

  Serial.print(F("Greenhouse started - automata wait "));
  Serial.print(STARTUP_DELAY_MS / 1000);
  Serial.println(F(" s"));
  printStatus();
}


// =====================================================================
//  LOOP - runs forever
// =====================================================================
void loop() {
  wdt_reset();   // "still alive" - must happen at least every 4 s
  // 1. Read the sensors every SAMPLE_MS (the moisture probe corrodes when powered)
  bool newSample = updateSensors();

  // 2. Initial cooldown: the automata don't run during the first
  //    STARTUP_DELAY_MS (they stay in IDLE, so every relay stays OFF).
  if (!automataStarted && millis() - startupTime >= STARTUP_DELAY_MS) {
    automataStarted = true;
    pumpStop = millis() - settings.COOLDOWN_MS;   // first watering allowed right away
    Serial.println(F("# initial cooldown over - automata running"));
  }

  //    Each automaton takes at most one transition.
  //    Runs every loop so the time limits are respected precisely.
  if (automataStarted) {
    byte before = irrigation.state();
    irrigation.run();
    if (irrigation.state() != before)
      logTransition("irrigation", IRRIGATION_NAME[before], IRRIGATION_NAME[irrigation.state()]);

    before = temperature.state();
    temperature.run();
    if (temperature.state() != before)
      logTransition("temperature", TEMPERATURE_NAME[before], TEMPERATURE_NAME[temperature.state()]);
  }

  // 3. The relays follow the current states.
  digitalWrite(PUMP_ACTUATOR,   pumpOn() ? PUMP_ON : PUMP_OFF);
  digitalWrite(HEATER_ACTUATOR, heaterOn() ? HEATER_ON : HEATER_OFF);
  digitalWrite(FAN_ACTUATOR,    fanOn()    ? FAN_ON    : FAN_OFF);

  // 4. Status line in the Serial Monitor
  if (newSample) printStatus();

  // 5. Screen + buttons (changing a setting takes effect at the next guard check)
  byte outputs = (pumpOn() ? 1 : 0)   // which relays are ON
               | (heaterOn() ? 2 : 0) | (fanOn() ? 4 : 0);    // (a change -> LCD refresh)
  DisplayData d = { moisture, T, waterRaw, tankOk, outputs };
  menuUpdate(d);
}
