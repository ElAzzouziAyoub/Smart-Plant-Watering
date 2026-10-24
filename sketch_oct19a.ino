// =====================================================================
//  SMART PLANT WATERING  -  moisture only (temperature comes later)
// =====================================================================
//
//  THE IDEA (what to tell your friends):
//    Every T seconds the Arduino:
//      1. reads the soil moisture M  (in %)
//      2. computes the error        e = M_REF - M
//      3. computes the pump time     u = Kp * e      (in seconds)
//      4. turns the pump ON for u seconds, then OFF for the rest of T
//
//  THE MATH MODEL (from our notes):
//      M[k+1] = M[k] + b * u[k]          b = moisture gained per second of pumping (%/s)
//      error:  e[k+1] = (1 - b*Kp) * e[k]
//      stable if  0 < Kp < 2/b    ->  we pick Kp = 0.5 / b  (smooth, no overshoot)
//
//  HOW TO USE THIS FILE - do the steps IN ORDER, change MODE each time:
//    MODE 1 : just read the sensor and print it         (check wiring)
//    MODE 2 : calibrate: note the raw value in dry air and in a glass of water
//    MODE 3 : measure b: run the pump for a fixed time and see how much M rises
//    MODE 4 : the real controller
// =====================================================================

#define MODE 1          // <-- CHANGE THIS: 1, 2, 3 or 4

// ---------------------------------------------------------------------
// PINS   (never use pins 0 and 1: they are used by the USB / Serial)
// ---------------------------------------------------------------------
#define MOISTURE_SENSOR        A0   // analog: moisture sensor output
#define WATER_LEVEL_SENSOR     A1   // analog: water level in the tank
#define MOISTURE_SENSOR_POWER  7    // digital: powers the sensor only while reading
                                    //          (stops a resistive probe from corroding)
#define PUMP_ACTUATOR          8    // digital: relay / MOSFET that drives the pump

// --- for later (temperature part), keep commented for now ---
// #include <max6675.h>
// #define TEMPERATURE_SENSOR_SCK 4
// #define TEMPERATURE_SENSOR_CS  5
// #define TEMPERATURE_SENSOR_SO  6
// #define FAN_ACTUATOR           9
// #define HEAT_RESISTANCE        10

// ---------------------------------------------------------------------
// CALIBRATION  (fill these in during MODE 2)
// ---------------------------------------------------------------------
int RAW_DRY = 1023;    // raw reading with the sensor in dry air     <-- TODO: measure
int RAW_WET = 300;     // raw reading with the sensor in water        <-- TODO: measure

int WATER_LEVEL_MIN = 100;  // below this raw value the tank is "empty" <-- TODO: measure

// Some relay modules turn ON with LOW instead of HIGH.
// If your pump runs when it should be off, swap these two.
#define PUMP_ON   HIGH
#define PUMP_OFF  LOW

// ---------------------------------------------------------------------
// CONTROLLER SETTINGS
// ---------------------------------------------------------------------
float M_REF = 60.0;          // target moisture in %                    <-- choose for your plant
float b     = 2.0;           // % of moisture gained per second of pump <-- TODO: measure in MODE 3
float Kp    = 0.5 / b;       // gain in seconds per %, inside 0 < Kp < 2/b

unsigned long T_PERIOD = 60;  // sampling period T in seconds (soil is slow: water needs time to soak)
float U_MAX = 5.0;            // max pump time per period in seconds (protects against flooding)

// For MODE 3
float TEST_PUMP_TIME = 3.0;   // seconds the pump runs during the test


// =====================================================================
//  SMALL HELPER FUNCTIONS
// =====================================================================

// Read the raw sensor value (0..1023). Power it, wait, read, power off.
int readMoistureRaw() {
  digitalWrite(MOISTURE_SENSOR_POWER, HIGH);
  delay(100);                              // give the sensor time to settle
  int raw = analogRead(MOISTURE_SENSOR);
  digitalWrite(MOISTURE_SENSOR_POWER, LOW);
  return raw;
}

// Convert the raw value to a percentage: 0% = dry, 100% = in water.
float readMoisturePercent() {
  int raw = readMoistureRaw();
  float percent = 100.0 * (RAW_DRY - raw) / (RAW_DRY - RAW_WET);
  return constrain(percent, 0, 100);
}

// True if there is enough water in the tank (never run the pump dry!)
bool tankHasWater() {
  return analogRead(WATER_LEVEL_SENSOR) > WATER_LEVEL_MIN;
}

// Run the pump for a given number of seconds.
void runPump(float seconds) {
  if (seconds <= 0) return;
  digitalWrite(PUMP_ACTUATOR, PUMP_ON);
  delay((unsigned long)(seconds * 1000));
  digitalWrite(PUMP_ACTUATOR, PUMP_OFF);
}


// =====================================================================
//  SETUP - runs once
// =====================================================================
void setup() {
  Serial.begin(9600);

  // Analog pins (A0, A1) don't need pinMode for analogRead.
  pinMode(MOISTURE_SENSOR_POWER, OUTPUT);
  pinMode(PUMP_ACTUATOR, OUTPUT);

  digitalWrite(PUMP_ACTUATOR, PUMP_OFF);   // safety: pump OFF at start

  Serial.print("Starting in MODE ");
  Serial.println(MODE);

#if MODE == 3
  // ---- MODE 3: measure b ----
  // Before: soil should be fairly dry. We read M, pump for a known time,
  // wait for water to soak in, read M again.
  //   b = (M_after - M_before) / TEST_PUMP_TIME
  // Do it 3 times and take the average, then put the value in "b" above.
  float before = readMoisturePercent();
  Serial.print("Moisture before: "); Serial.println(before);

  runPump(TEST_PUMP_TIME);
  Serial.println("Pump done, waiting 60 s for the water to soak...");
  delay(60000);

  float after = readMoisturePercent();
  Serial.print("Moisture after:  "); Serial.println(after);
  Serial.print("b = ");
  Serial.print((after - before) / TEST_PUMP_TIME);
  Serial.println(" %/s");
#endif
}


// =====================================================================
//  LOOP - runs forever
// =====================================================================
void loop() {

#if MODE == 1
  // ---- MODE 1: check wiring ----
  // Open Serial Monitor (9600). Touch the sensor / put it in soil:
  // the numbers should change.
  Serial.print("Moisture raw: ");
  Serial.print(readMoistureRaw());
  Serial.print("   Water level raw: ");
  Serial.println(analogRead(WATER_LEVEL_SENSOR));
  delay(1000);

#elif MODE == 2
  // ---- MODE 2: calibration ----
  // 1. Sensor in dry air     -> write the number into RAW_DRY
  // 2. Sensor in a glass of water -> write the number into RAW_WET
  // 3. Tank almost empty     -> write the water level number into WATER_LEVEL_MIN
  // Then check that the % below goes from ~0 (dry) to ~100 (water).
  Serial.print("Raw: ");
  Serial.print(readMoistureRaw());
  Serial.print("   ->  ");
  Serial.print(readMoisturePercent());
  Serial.println(" %");
  delay(1000);

#elif MODE == 3
  // Nothing: the test runs once in setup(). Press RESET to repeat it.

#elif MODE == 4
  // ---- MODE 4: the controller ----

  // 1. Measure
  float M = readMoisturePercent();

  // 2. Error (positive = soil too dry)
  float e = M_REF - M;

  // 3. Proportional controller: pump time u = Kp * e
  float u = Kp * e;

  // 4. Limits: can't pump negative time, and never more than U_MAX
  //    (if the soil is too wet, u < 0 -> pump stays OFF and we wait for it to dry)
  u = constrain(u, 0, U_MAX);

  // 5. Safety: no water in tank -> no pumping
  if (!tankHasWater()) {
    u = 0;
    Serial.println("WARNING: tank empty!");
  }

  // Print in "name:value" format so Tools > Serial Plotter can draw curves
  Serial.print("M:");     Serial.print(M);
  Serial.print(" M_REF:"); Serial.print(M_REF);
  Serial.print(" u:");     Serial.println(u);

  // 6. Pump ON for u seconds, OFF for the rest of the period T
  runPump(u);
  delay((T_PERIOD * 1000UL) - (unsigned long)(u * 1000));
#endif
}

// =====================================================================
//  NEXT STEPS (after MODE 4 works)
//   - Log M every period for a day with the pump OFF -> measure drying
//     and get "a" for the better model  M[k+1] = a*M[k] + b*u[k] + c
//   - Try different Kp values and compare with the theory (0 < Kp < 2/b)
//   - Add the integral term (PI) so the moisture really reaches M_REF
//     even while the soil keeps drying
//   - Then add temperature (MAX6675), fan and heater
// =====================================================================
