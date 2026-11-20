#pragma once
#include <Arduino.h>

// =====================================================================
//  SCREEN (16x2 LCD, I2C backpack: SDA=A4, SCL=A5, VCC=5V, GND)
//  + 5 BUTTONS (each between its pin and GND)
//
//  LIVE METRICS (startup): moisture, temperature, and tank water level.
//      MENU -> MAIN MENU
//
//  MAIN MENU: Edit thresholds / Actual metrics
//      UP -> Edit thresholds; DOWN -> Actual metrics; OK -> open it
//      MENU or CANCEL -> live metrics
//
//  EDIT THRESHOLDS:
//    choosing a threshold:
//      UP/DOWN -> previous / next threshold (one step per press; wraps)
//      OK -> start changing it
//      MENU or CANCEL -> main menu
//    changing its value:
//      UP/DOWN -> + / - (hold = repeat, hold > 2 s = 10x faster)
//      OK -> save to EEPROM (invalid combinations are refused)
//      CANCEL -> discard draft, return to threshold list
//      MENU -> discard draft, return to main menu
//    no button for 30 s in either menu -> live metrics
// =====================================================================
#define BUTTON_MENU_PIN   2
#define BUTTON_OK_PIN     3
#define BUTTON_CANCEL_PIN 4
#define BUTTON_UP_PIN     5
#define BUTTON_DOWN_PIN   6

// Filled by the main sketch every loop.
struct DisplayData {
  int   moisture; // moisture %, 0 = dry .. 100 = wet
  float T;        // temperature C (NAN = no reading)
  int   water;    // water level sensor, raw 0..1023
  bool  tankOk;   // water level above the "tank empty" threshold?
  byte  outputs;  // bit0 pump, bit1 heater, bit2 fan (a change = relay noise -> LCD refresh)
};

void menuBegin();                        // call once in setup()
void menuUpdate(const DisplayData& d);   // call every loop (never blocks)
