// =====================================================================
//  SMART PLANT WATERING  -  moisture only (temperature comes later)
//  Model: HYBRID AUTOMATON with 2 states (OFF / ON) and hysteresis
// =====================================================================
//
//  THE IDEA (what to tell your friends):
//    The pump is either OFF or ON. It is a little "state machine":
//
//              M <= M_LOW
//        +-----+  ------------>  +----+
//        | OFF |                 | ON |
//        +-----+  <------------  +----+
//              M >= M_STOP  (or pump ON for too long)
//
//    OFF : P = 0   ->  dM/dt = -d(M)          (soil dries)
//    ON  : P = 1   ->  dM/dt = r/V - d(M)     (pump adds water)
//
//    - Pump turns ON  when the soil is too dry      (M <= M_LOW)
//    - Pump turns OFF a bit BEFORE the target       (M >= M_STOP = M_REF - EPS)
//      because water keeps spreading after the pump stops (no overshoot)
//    - Two different thresholds = HYSTERESIS: the pump can't flicker
//      ON/OFF/ON/OFF when the reading is noisy.
//
//  WHAT WE PROVE (instead of poles):
//    - Safety      : M stays between M_LOW and M_REF (no flooding)
//    - No chattering: two thresholds + minimum time in each state
//    - Safety timer: pump never ON longer than MAX_ON_TIME
//
//  HOW TO USE THIS FILE - do the steps IN ORDER, change MODE each time:
//    MODE 1 : just read the sensor and print it         (check wiring)
//    MODE 2 : calibrate: raw value in dry air and in a glass of water
//    MODE 3 : pump test: how fast M rises (V_MAX) and how much it keeps
//             rising after the pump stops (RESIDUAL)  ->  choose EPS
//    MODE 4 : the real automaton
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
// AUTOMATON SETTINGS
// ---------------------------------------------------------------------
float M_REF  = 60.0;          // target moisture in %                  <-- choose for your plant
float EPS    = 5.0;           // stop margin in %   (EPS >= V_MAX*TAU + RESIDUAL, see MODE 3)
float M_STOP = M_REF - EPS;   // pump ON -> OFF when M >= M_STOP   (55 %)
float M_LOW  = 45.0;          // pump OFF -> ON when M <= M_LOW

unsigned long SAMPLE_ON  = 1;     // seconds between readings while pump is ON  (watch closely!)
unsigned long SAMPLE_OFF = 60;    // seconds between readings while pump is OFF (soil dries slowly)
unsigned long MAX_ON_TIME = 30;   // safety: pump never ON longer than this (s)
unsigned long MIN_OFF_TIME = 300; // after watering, wait at least this long (s) so the
                                  // water can spread before we decide again

// For MODE 3
float TEST_PUMP_TIME = 3.0;   // seconds the pump runs during the test
int   TEST_WATCH_TIME = 120;  // seconds we keep reading after the pump stops


// =====================================================================
//  THE STATE OF THE AUTOMATON
// =====================================================================
enum PumpState { OFF, ON };
PumpState state = OFF;

unsigned long stateStartTime = 0;   // millis() when we entered the current state

// seconds spent in the current state
unsigned long timeInState() {
  return (millis() - stateStartTime) / 1000;
}


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

// Change state: switch the pump and remember when it happened.
void goTo(PumpState newState) {
  state = newState;
  stateStartTime = millis();
  digitalWrite(PUMP_ACTUATOR, state == ON ? PUMP_ON : PUMP_OFF);
}


// =====================================================================
//  SETUP - runs once
// =====================================================================
void setup() {
  Serial.begin(9600);

  // Analog pins (A0, A1) don't need pinMode for analogRead.
  pinMode(MOISTURE_SENSOR_POWER, OUTPUT);
  pinMode(PUMP_ACTUATOR, OUTPUT);

  goTo(OFF);                    // safety: pump OFF at start
  stateStartTime -= MIN_OFF_TIME * 1000UL;   // ...but allow watering right away

  Serial.print("Starting in MODE ");
  Serial.println(MODE);

#if MODE == 3
  // ---- MODE 3: pump test ----
  // Soil should be fairly dry. Open Tools > Serial Plotter.
  //   Phase 1: pump ON for TEST_PUMP_TIME, M read every second
  //            -> V_MAX = biggest rise per second
  //   Phase 2: pump OFF, keep reading for TEST_WATCH_TIME
  //            -> RESIDUAL = how much M still rises after the pump stopped
  // Then choose:  EPS >= V_MAX * SAMPLE_ON + RESIDUAL
  float before = readMoisturePercent();

  goTo(ON);
  float previous = before;
  float vMax = 0;
  while (timeInState() < TEST_PUMP_TIME) {
    delay(1000);
    float M = readMoisturePercent();
    vMax = max(vMax, M - previous);
    previous = M;
    Serial.print("M:"); Serial.print(M); Serial.println(" pump:100");
  }
  goTo(OFF);
  float atStop = readMoisturePercent();

  float highest = atStop;
  for (int i = 0; i < TEST_WATCH_TIME; i++) {
    delay(1000);
    float M = readMoisturePercent();
    highest = max(highest, M);
    Serial.print("M:"); Serial.print(M); Serial.println(" pump:0");
  }

  Serial.print("V_MAX    = "); Serial.print(vMax);             Serial.println(" %/s");
  Serial.print("RESIDUAL = "); Serial.print(highest - atStop); Serial.println(" %");
  Serial.print("-> choose EPS >= ");
  Serial.println(vMax * SAMPLE_ON + (highest - atStop));
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
  // 1. Sensor in dry air          -> write the number into RAW_DRY
  // 2. Sensor in a glass of water -> write the number into RAW_WET
  // 3. Tank almost empty          -> write the water level number into WATER_LEVEL_MIN
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
  // ---- MODE 4: the hybrid automaton ----

  // 1. Measure
  float M = readMoisturePercent();

  // 2. Transitions (the arrows of the diagram)
  if (state == OFF) {
    // OFF -> ON : soil too dry, water spread long enough, and tank not empty
    if (M <= M_LOW && timeInState() >= MIN_OFF_TIME && tankHasWater()) {
      goTo(ON);
    }
  }
  else {  // state == ON
    // ON -> OFF : target reached (minus margin), OR safety timer, OR tank empty
    if (M >= M_STOP) {
      goTo(OFF);
    }
    else if (timeInState() >= MAX_ON_TIME) {
      goTo(OFF);
      Serial.println("WARNING: max ON time reached (sensor problem?)");
    }
    else if (!tankHasWater()) {
      goTo(OFF);
      Serial.println("WARNING: tank empty!");
    }
  }

  // 3. Print in "name:value" format so Tools > Serial Plotter can draw curves
  Serial.print("M:");       Serial.print(M);
  Serial.print(" M_LOW:");  Serial.print(M_LOW);
  Serial.print(" M_STOP:"); Serial.print(M_STOP);
  Serial.print(" pump:");   Serial.println(state == ON ? 100 : 0);

  // 4. Wait: read often while watering, rarely while drying
  delay((state == ON ? SAMPLE_ON : SAMPLE_OFF) * 1000UL);
#endif
}

// =====================================================================
//  NEXT STEPS (after MODE 4 works)
//   - Log M for a day with the pump OFF -> measure the drying d(M)
//   - Check experimentally: does M always stay in [M_LOW, M_REF]? (safety)
//   - Count pump switches per day (no chattering)
//   - Then add temperature (MAX6675), fan and heater: same automaton idea
// =====================================================================
