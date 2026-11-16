#include "Menu.h"
#include "Settings.h"
#include <Wire.h>
#include <avr/pgmspace.h>
#include <hd44780.h>                        // "hd44780" library by Bill Perry
#include <hd44780ioClass/hd44780_I2Cexp.h>  // I2C backpack, address auto-detected

// The Uno has only 2 KB of RAM: every fixed text stays in flash (PROGMEM/PSTR/F).

// =====================================================================
//  LCD  (with automatic recovery)
// =====================================================================
// A relay switching (pump/heater/fan) makes a spike on 5V that can knock
// the LCD out of step -> random symbols. So the LCD is initialised again
// shortly after any relay change, and every 30 s anyway.
static hd44780_I2Cexp lcd;
static bool lcdOk = false;
static unsigned long lcdInitAt = 0;                    // last init (or failed try)
static const unsigned long LCD_RETRY_MS       = 5000;  // not found -> try again
static const unsigned long LCD_REINIT_MS      = 30000; // periodic refresh
static const unsigned long LCD_AFTER_RELAY_MS = 300;   // wait for the spike to pass

static bool initLcd() {
  lcdInitAt = millis();
  int status = lcd.begin(16, 2);                       // 0 = OK
  if (status != 0) {
    Serial.print(F("LCD not ready (status "));
    Serial.print(status);
    Serial.println(F(") - check SDA=A4, SCL=A5, 5V, GND. Retrying..."));
    return false;
  }
  return true;
}

// Writes one full 16-char line (padded with spaces: no clear(), no flicker).
static void printLine(byte row, const char* text) {
  char line[17];
  snprintf_P(line, sizeof(line), PSTR("%-16s"), text);
  lcd.setCursor(0, row);
  lcd.print(line);
}

// =====================================================================
//  BUTTONS  (debounced; UP/DOWN repeat only while editing values)
// =====================================================================
enum { B_MENU, B_OK, B_CANCEL, B_UP, B_DOWN, B_COUNT };
static const byte BUTTON_PIN[B_COUNT] = {
  BUTTON_MENU_PIN, BUTTON_OK_PIN, BUTTON_CANCEL_PIN, BUTTON_UP_PIN, BUTTON_DOWN_PIN
};
static const unsigned long DEBOUNCE_MS = 30, REPEAT_DELAY_MS = 500,
                           REPEAT_EVERY_MS = 120, FAST_AFTER_MS = 2000;

struct Button { bool stable, raw; unsigned long changedAt, pressedAt, lastRepeat; };
static Button btn[B_COUNT];
static byte events = 0;           // bit i = button i fired in this update

static void printButtonName(byte i) {
  switch (i) {
    case B_MENU:   Serial.print(F("MENU"));   break;
    case B_OK:     Serial.print(F("OK"));     break;
    case B_CANCEL: Serial.print(F("CANCEL")); break;
    case B_UP:     Serial.print(F("UP"));     break;
    default:       Serial.print(F("DOWN"));   break;
  }
}

static void readButtons(bool repeatValues) {
  unsigned long now = millis();
  events = 0;
  for (byte i = 0; i < B_COUNT; ++i) {
    Button& b = btn[i];
    // INPUT_PULLUP: released = HIGH, pressed (connected to GND) = LOW.
    // Never learn polarity at boot: a held button or noise would invert it.
    bool raw = digitalRead(BUTTON_PIN[i]) == LOW;
    if (raw != b.raw) { b.raw = raw; b.changedAt = now; }
    if (raw != b.stable && now - b.changedAt >= DEBOUNCE_MS) {
      b.stable = raw;
      if (raw) {
        events |= bit(i);
        b.pressedAt = b.lastRepeat = now;
        Serial.print(F("[button] ")); printButtonName(i); Serial.println();
      }
    } else if (repeatValues && raw && b.stable && (i == B_UP || i == B_DOWN)
               && now - b.pressedAt >= REPEAT_DELAY_MS
               && now - b.lastRepeat >= REPEAT_EVERY_MS) {
      events |= bit(i);
      b.lastRepeat = now;
    }
  }
}

static bool fired(byte i)    { return events & bit(i); }
static bool heldLong(byte i) { return btn[i].stable && millis() - btn[i].pressedAt >= FAST_AFTER_MS; }

// =====================================================================
//  THE THRESHOLDS THAT CAN BE MODIFIED  (table in flash)
// =====================================================================
enum Kind : byte { K_PERCENT, K_CELSIUS, K_SECONDS, K_RAW };
struct Item { const char* label; Kind kind; void* value; float minV, maxV, step; };

static const char L0[]  PROGMEM = "Hlow (start <=)";
static const char L1[]  PROGMEM = "Htarget (stop >=)";
static const char L2[]  PROGMEM = "Pump max time";
static const char L3[]  PROGMEM = "Pump cooldown";
static const char L4[]  PROGMEM = "Tmin (heating)";
static const char L6[]  PROGMEM = "Tmax (cooling)";
static const char L8[]  PROGMEM = "Cooling min on";
static const char L9[]  PROGMEM = "Raw dry (air)";
static const char L10[] PROGMEM = "Raw wet (water)";
static const char L11[] PROGMEM = "Tank empty <";

static const Item ITEMS[] PROGMEM = {
  { L0,  K_PERCENT, &settings.H_LOW,             0,  100, 1   },
  { L1,  K_PERCENT, &settings.H_TARGET,          0,  100, 1   },
  { L2,  K_SECONDS, &settings.PUMP_MAX_MS,       1,  300, 1   },
  { L3,  K_SECONDS, &settings.COOLDOWN_MS,       0, 3600, 5   },
  { L4,  K_CELSIUS, &settings.T_LOW,           -10,   80, 0.5 },
  { L6,  K_CELSIUS, &settings.T_HIGH,          -10,   80, 0.5 },
  { L8,  K_SECONDS, &settings.THERMAL_MIN_ON_MS, 0,  600, 5   },
  { L9,  K_RAW,     &settings.RAW_DRY,           0, 1023, 1   },
  { L10, K_RAW,     &settings.RAW_WET,           0, 1023, 1   },
  { L11, K_RAW,     &settings.WATER_LEVEL_MIN,   0, 1023, 1   },
};
static const byte ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

static Item item(byte i) { Item it; memcpy_P(&it, &ITEMS[i], sizeof(Item)); return it; }

// Every value is edited as a float, then converted back to its real type.
static float getValue(const Item& it) {
  switch (it.kind) {
    case K_SECONDS: return *(unsigned long*)it.value / 1000.0;
    case K_RAW:     return *(int*)it.value;
    default:        return *(float*)it.value;
  }
}
static void setValue(const Item& it, float v) {
  switch (it.kind) {
    case K_SECONDS: *(unsigned long*)it.value = (unsigned long)(v * 1000.0 + 0.5); break;
    case K_RAW:     *(int*)it.value = (int)(v + 0.5); break;
    default:        *(float*)it.value = v; break;
  }
}
// "40 %", "45.0 C", "10 s", "979"
static void formatValue(const Item& it, float v, char* out) {
  dtostrf(v, 0, it.kind == K_CELSIUS ? 1 : 0, out);     // AVR printf has no %f
  if      (it.kind == K_PERCENT) strcat_P(out, PSTR(" %"));
  else if (it.kind == K_CELSIUS) strcat_P(out, PSTR(" C"));
  else if (it.kind == K_SECONDS) strcat_P(out, PSTR(" s"));
}

// =====================================================================
//  LIVE METRICS, MAIN MENU, AND THRESHOLD EDITOR
// =====================================================================
enum Mode : byte { NORMAL, MAIN_MENU, MODIFY };
static Mode mode = NORMAL;
static byte mainSel = 0;            // 0 = edit thresholds, 1 = actual metrics
static bool editing = false;        // MODIFY: false = choosing, true = changing the value
static byte sel = 0;                // selected threshold
static float editValue = 0;
static PGM_P note = nullptr;        // short message on line 2 ("Saved", ...)
static unsigned long noteUntil = 0, lastActivity = 0, lastDraw = 0;
static bool redraw = true;
static const unsigned long TIMEOUT_MS = 30000, NOTE_MS = 1500, NORMAL_REFRESH_MS = 500;

static void showNote(PGM_P text) { note = text; noteUntil = millis() + NOTE_MS; }

static void draw(const DisplayData& d) {
  char a[17], b[17], v[12];
  if (mode == NORMAL) {
    // H:45%  T:23.5C
    // Water:412 OK
    char h[6], t[8], w[6];
    dtostrf(d.H, 0, 0, h);
    if (isnan(d.T)) strcpy_P(t, PSTR("--.-")); else dtostrf(d.T, 0, 1, t);
    strcpy_P(w, d.tankOk ? PSTR("OK") : PSTR("EMPTY"));
    snprintf_P(a, sizeof(a), PSTR("H:%s%%  T:%sC"), h, t);
    snprintf_P(b, sizeof(b), PSTR("Water:%d %s"), d.water, w);
  } else if (mode == MAIN_MENU) {
    // Both choices fit the two-row LCD; > marks the current choice.
    snprintf_P(a, sizeof(a), PSTR("%cEdit thresholds"), mainSel == 0 ? '>' : ' ');
    snprintf_P(b, sizeof(b), PSTR("%cActual metrics"), mainSel == 1 ? '>' : ' ');
  } else {
    Item it = item(sel);
    strncpy_P(a, it.label, 16); a[16] = '\0';
    if (note && (long)(millis() - noteUntil) < 0) {
      strncpy_P(b, note, 16); b[16] = '\0';                        // Saved / Not saved
    } else if (editing) {
      formatValue(it, editValue, v);
      snprintf_P(b, sizeof(b), PSTR("> %s"), v);                   // > 41 %
    } else {
      if (sel == 1 && getValue(it) == 0) strcpy_P(v, PSTR("0 % OFF"));
      else formatValue(it, getValue(it), v);
      snprintf_P(b, sizeof(b), PSTR("%-11s%2d/%d"), v, sel + 1, ITEM_COUNT);   // 40 %   1/12
    }
  }
  printLine(0, a);
  printLine(1, b);
}

static void goNormal() { mode = NORMAL; editing = false; note = nullptr; }

static void goMainMenu() { mode = MAIN_MENU; editing = false; note = nullptr; }

static void handleButtons() {
  if (mode == NORMAL) {
    if (fired(B_MENU)) { mainSel = 0; goMainMenu(); }
    return;
  }
  if (mode == MAIN_MENU) {
    if (fired(B_MENU) || fired(B_CANCEL)) { goNormal(); return; }
    if (fired(B_OK)) {
      if (mainSel == 0) { mode = MODIFY; sel = 0; }
      else goNormal();
      return;
    }
    // Direction selects a fixed row; a held/noisy key cannot toggle rows.
    if (fired(B_UP)) mainSel = 0;
    else if (fired(B_DOWN)) mainSel = 1;
    return;
  }
  if (!editing) {                                          // MODIFY: choosing
    note = nullptr;
    if (fired(B_MENU) || fired(B_CANCEL)) { goMainMenu(); return; }
    if (fired(B_UP))   sel = (sel == 0) ? ITEM_COUNT - 1 : sel - 1;
    if (fired(B_DOWN)) sel = (sel + 1) % ITEM_COUNT;
    if (fired(B_OK))   { editing = true; editValue = getValue(item(sel)); }
    return;
  }
  Item it = item(sel);                                     // MODIFY: changing the value
  note = nullptr;
  // Exit buttons take priority, including when another key is pressed.
  if (fired(B_MENU)) { goMainMenu(); return; }
  if (fired(B_CANCEL)) { editing = false; return; }
  if (fired(B_UP))   editValue = min(editValue + it.step * (heldLong(B_UP)   ? 10 : 1), it.maxV);
  if (fired(B_DOWN)) editValue = max(editValue - it.step * (heldLong(B_DOWN) ? 10 : 1), it.minV);
  if (fired(B_OK)) {
    float old = getValue(it);
    setValue(it, editValue);
    if (settingsValid(settings)) {
      saveSettings(); showNote(PSTR("Saved")); editing = false;
    } else {
      setValue(it, old); showNote(PSTR("Invalid value"));
      // Keep the draft open so it can be corrected or cancelled.
    }
  }
}

// =====================================================================
//  PUBLIC
// =====================================================================
void menuBegin() {
  // Fixed active-low wiring; a button held during reset is still a press.
  for (byte i = 0; i < B_COUNT; ++i) pinMode(BUTTON_PIN[i], INPUT_PULLUP);
  delay(10);
  Serial.print(F("Buttons (released=1, pressed=0; wire each pin to GND):"));
  for (byte i = 0; i < B_COUNT; ++i) {
    bool pressed = digitalRead(BUTTON_PIN[i]) == LOW;
    btn[i] = { false, pressed, millis(), 0, 0 };
    Serial.print(' '); printButtonName(i); Serial.print('='); Serial.print(pressed ? 0 : 1);
  }
  Serial.println();

  // I2C: never wait forever on a stuck bus (25 ms timeout + reset)
  Wire.begin();
  Wire.setWireTimeout(25000, true);
  lcdOk = initLcd();
  redraw = true;
}

void menuUpdate(const DisplayData& d) {
  unsigned long now = millis();
  static bool first = true;
  static byte lastOutputs = 0;
  static unsigned long outputsChangedAt = 0;
  static bool reinitPending = false;

  // --- LCD health ---
  if (first) { lastOutputs = d.outputs; first = false; }
  if (d.outputs != lastOutputs) {                 // a relay just switched
    lastOutputs = d.outputs; outputsChangedAt = now; reinitPending = true;
  }
  if (!lcdOk) {
    if (now - lcdInitAt >= LCD_RETRY_MS) { lcdOk = initLcd(); redraw = true; }
  } else if ((reinitPending && now - outputsChangedAt >= LCD_AFTER_RELAY_MS)
             || now - lcdInitAt >= LCD_REINIT_MS) {
    lcdOk = initLcd();                            // clean restart of the LCD
    reinitPending = false;
    redraw = true;
  }

  // --- buttons + modes (work even if the LCD is missing) ---
  readButtons(mode == MODIFY && editing);
  if (events) { lastActivity = now; redraw = true; handleButtons(); }
  if (mode != NORMAL && now - lastActivity >= TIMEOUT_MS) { goNormal(); redraw = true; }

  // --- drawing ---
  if (note && (long)(now - noteUntil) >= 0) { note = nullptr; redraw = true; }
  if (mode == NORMAL && now - lastDraw >= NORMAL_REFRESH_MS) redraw = true;
  if (redraw && lcdOk) {
    draw(d);
    lastDraw = now;
    redraw = false;
  }
}
