#include "Settings.h"
#include <EEPROM.h>
#include <avr/pgmspace.h>

// Factory values (the old hardcoded constants). Kept in flash (PROGMEM) to save RAM.
static const Settings DEFAULT_SETTINGS PROGMEM = {
  5.0,       // H_LOW        temporary dry-soil test
  10.0,      // H_TARGET     stop watering at 10%
  10000UL,   // PUMP_MAX_MS
  60000UL,   // COOLDOWN_MS
  30.0,      // T_LOW        (heat below 30 C)
  40.0,      // T_HIGH       (cool at or above 40 C)
  15000UL,   // THERMAL_MIN_ON_MS
  1023,      // RAW_DRY        <-- measure
  300,       // RAW_WET        <-- measure
  100        // WATER_LEVEL_MIN <-- measure
};

Settings settings;   // filled by loadSettings() at the start of setup()

static void loadDefaults() { memcpy_P(&settings, &DEFAULT_SETTINGS, sizeof(Settings)); }

// EEPROM layout: [MAGIC][VERSION][Settings...]
// Change VERSION when stored settings need migration or the layout changes.
static const byte MAGIC = 0x47;   // 'G' for Greenhouse
static const byte VERSION = 11;  // 11: enable watering with test thresholds 5/10%
static const int ADDR = 0;

bool settingsValid(const Settings& s) {
  return s.H_LOW >= 0 && s.H_LOW <= 100
      && s.H_TARGET >= 0 && s.H_TARGET <= 100
      && (s.H_TARGET == 0 || s.H_LOW < s.H_TARGET)  // 0 disables watering
      && s.T_LOW >= -10 && s.T_HIGH <= 80 && s.T_LOW < s.T_HIGH
      && s.PUMP_MAX_MS > 0
      && s.RAW_DRY != s.RAW_WET           // otherwise division by zero
      && s.RAW_DRY >= 0 && s.RAW_DRY <= 1023
      && s.RAW_WET >= 0 && s.RAW_WET <= 1023
      && s.WATER_LEVEL_MIN >= 0 && s.WATER_LEVEL_MIN <= 1023;
}

void loadSettings() {
  const byte storedVersion = EEPROM.read(ADDR + 1);
  if (EEPROM.read(ADDR) == MAGIC && (storedVersion == VERSION || storedVersion == 10)) {
    Settings stored;
    EEPROM.get(ADDR + 2, stored);
    if (settingsValid(stored)) {
      settings = stored;
      if (storedVersion == 10) {
        settings.H_LOW = 5.0;
        settings.H_TARGET = 10.0;
        saveSettings();
      }
      return;
    }
  } else if (EEPROM.read(ADDR) == MAGIC && storedVersion >= 5 && storedVersion <= 9) {
    // Previous layout: retain non-temperature settings, replace the four limits.
    struct LegacySettings {
      float H_LOW, H_TARGET;
      unsigned long PUMP_MAX_MS, COOLDOWN_MS;
      float T_LOW, T_HEAT_STOP, T_HIGH, T_COOL_STOP;
      unsigned long THERMAL_MIN_ON_MS;
      int RAW_DRY, RAW_WET, WATER_LEVEL_MIN;
    } old;
    EEPROM.get(ADDR + 2, old);
    loadDefaults();
    settings.H_LOW = 5.0;
    settings.H_TARGET = 10.0;
    settings.PUMP_MAX_MS = old.PUMP_MAX_MS;
    settings.COOLDOWN_MS = old.COOLDOWN_MS;
    settings.THERMAL_MIN_ON_MS = old.THERMAL_MIN_ON_MS;
    settings.RAW_DRY = old.RAW_DRY;
    settings.RAW_WET = old.RAW_WET;
    settings.WATER_LEVEL_MIN = old.WATER_LEVEL_MIN;
    if (settingsValid(settings)) { saveSettings(); return; }
  }
  loadDefaults();   // first start, or corrupted data
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
