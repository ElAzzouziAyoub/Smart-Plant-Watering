#include "Settings.h"
#include <EEPROM.h>
#include <avr/pgmspace.h>

// Factory values (the old hardcoded constants). Kept in flash (PROGMEM) to save RAM.
static const Settings DEFAULT_SETTINGS PROGMEM = {
  35,        // H_TARGET     H < 35 % -> water 2 s, H > 35 % -> nothing  <-- choose for your plant
  2000UL,    // PUMP_MAX_MS  2 s max per watering
  900000UL,  // COOLDOWN_MS  15 min for the water to soak in
  18.0,      // T_LOW        (heat below 18 C)
  28.0,      // T_HIGH       (cool at or above 28 C)
  60000UL,   // THERMAL_MIN_ON_MS  fan runs at least 60 s
  0          // WATER_LEVEL_MIN <-- measure (0 for now: any reading > 0 = tank ok)
};

Settings settings;   // filled by loadSettings() at the start of setup()

static void loadDefaults() { memcpy_P(&settings, &DEFAULT_SETTINGS, sizeof(Settings)); }

// EEPROM layout: [MAGIC][VERSION][Settings...]
// Change VERSION when stored settings need migration or the layout changes.
static const byte MAGIC = 0x47;   // 'G' for Greenhouse
static const byte VERSION = 17;  // 17: one moisture threshold in % (H_TARGET, same slot as the old H_MIN)
static const int ADDR = 0;

bool settingsValid(const Settings& s) {
  return s.H_TARGET >= 0 && s.H_TARGET <= 100     // 0 disables watering
      && s.T_LOW >= -10 && s.T_HIGH <= 80 && s.T_LOW < s.T_HIGH
      && s.PUMP_MAX_MS > 0
      && s.WATER_LEVEL_MIN >= 0 && s.WATER_LEVEL_MIN <= 1023;
}

void loadSettings() {
  if (EEPROM.read(ADDR) == MAGIC && EEPROM.read(ADDR + 1) == VERSION) {
    Settings stored;
    EEPROM.get(ADDR + 2, stored);
    if (settingsValid(stored)) { settings = stored; return; }
  }
  // First start, older layout (raw thresholds), or corrupted data:
  // start from the defaults and save them.
  resetSettings();
}

void saveSettings() {
  EEPROM.update(ADDR, MAGIC);      // update() only writes bytes that changed
  EEPROM.update(ADDR + 1, VERSION);
  EEPROM.put(ADDR + 2, settings);  // put() also uses update() internally
}

void resetSettings() {
  loadDefaults();
  saveSettings();
}
