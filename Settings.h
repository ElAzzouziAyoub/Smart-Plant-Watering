#pragma once
#include <Arduino.h>

// All the values the user can change (from the screen menu).
// The automata's guards read them through the global 'settings'.
// They are saved in EEPROM, so they survive a reset / power loss.
struct Settings {
  // Moisture in % (0 = dry air, 100 = in water; calibration in GreenhouseSimple.ino).
  int H_TARGET;                   // %: H < Htarget -> water 2 s, wait cooldown, read again; 0 disables the pump
  unsigned long PUMP_MAX_MS;      // pump never runs longer than this in one go
  unsigned long COOLDOWN_MS;      // minimum wait after the pump stops

  // Temperature: one lower limit and one upper limit
  float T_LOW;                    // C : below -> heating; at or above -> stop heating
  float T_HIGH;                   // C : at or above -> cooling; below -> stop cooling
  unsigned long THERMAL_MIN_ON_MS;// cooling fan stays ON at least this long

  // Tank
  int WATER_LEVEL_MIN;            // water raw below this = tank "empty"
};

extern Settings settings;               // the values in use (defaults: Settings.cpp)

void loadSettings();                    // from EEPROM (defaults if empty/invalid) - call first in setup()
void saveSettings();                    // to EEPROM
void resetSettings();                   // back to defaults (and save)
bool settingsValid(const Settings& s);  // the thresholds make sense together
