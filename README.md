# Smart Plant Watering (Arduino)

An Arduino keeps a plant's soil moist using a **hybrid automaton**: a 2-state machine (pump OFF / pump ON) with **hysteresis** thresholds.

This first version controls **moisture only**. Temperature (MAX6675), the fan and the heater come later.

---

## 1. The idea

The pump is binary: it's either ON or OFF. So we model it the way it really works, as a state machine, instead of pretending it's a continuous actuator.

```
              M <= M_LOW
        +-----+  ------------>  +----+
        | OFF |                 | ON |
        +-----+  <------------  +----+
              M >= M_STOP
```

| State | Pump | Moisture dynamics       |
|-------|------|-------------------------|
| OFF   | P = 0 | dM/dt = −d(M)          (the soil dries) |
| ON    | P = 1 | dM/dt = r/V − d(M)     (the pump adds water) |

- `r`: pump flow rate, `V`: effective soil volume, `d(M)`: moisture loss (evaporation, drainage, the plant drinking)
- The **continuous** moisture dynamics combined with **discrete** switching make this a *hybrid system* (Lee & Seshia, Ch. 4).

### Transitions

```
q[k+1] = ON    if q[k] = OFF and M[k] <= M_LOW
         OFF   if q[k] = ON  and M[k] >= M_STOP
         q[k]  otherwise
```

### Why two thresholds (hysteresis)?
With a single threshold, a noisy reading around 55% would switch the pump ON/OFF/ON/OFF very quickly (**chattering**). With two thresholds `M_LOW = 45%` and `M_STOP = 55%`, the system cycles cleanly: it fills up to 55%, then dries down to 45%, and repeats.

### Why stop before the target (the margin ε)?
```
M_STOP = M_REF − ε        e.g. 60 − 5 = 55 %
```
After the pump stops, the water keeps spreading through the soil and the reading can keep rising. Stopping `ε` early prevents **overshooting** the target.

`ε` must be chosen from measurements, not guessed:

```
ε ≥ V_MAX · τ + RESIDUAL
```
- `V_MAX`: fastest moisture rise while the pump is ON (%/s)
- `τ`: worst-case delay between readings while ON (`SAMPLE_ON` = 1 s)
- `RESIDUAL`: how much M still rises after the pump stops

MODE 3 measures these for you.

---

## 2. What we prove (instead of poles)

A switched system doesn't settle at one equilibrium point; it **cycles inside a band**. That's expected, not instability. So instead of checking `|z| < 1`, we check:

| Property          | Meaning                                         | Ensured by                    |
|-------------------|-------------------------------------------------|-------------------------------|
| **Safety**        | M never goes above `M_REF` (no flooding)        | margin `ε`, `MAX_ON_TIME`     |
| **Boundedness**   | M stays in a band around `[M_LOW, M_STOP]`      | hysteresis thresholds         |
| **No chattering** | no infinitely fast switching                    | 2 thresholds + `MIN_OFF_TIME` |
| **Reachability**  | when dry, the pump eventually reaches `M_STOP`  | `r/V > d(M)`: the pump adds water faster than the soil loses it |

---

## 3. Hardware and wiring

| Component                    | Arduino pin |
|------------------------------|-------------|
| Moisture sensor (signal)     | `A0`        |
| Water level sensor (signal)  | `A1`        |
| Moisture sensor power (VCC)  | `D7`        |
| Pump relay / MOSFET input    | `D8`        |

Notes:
- **Don't use pins 0 and 1**. They carry the USB serial connection.
- The moisture sensor is powered from `D7` only while it's being read. This stops a resistive probe from corroding.
- Never power the pump directly from an Arduino pin. Use a relay module or a MOSFET.
- Some relay modules switch ON with `LOW`. If the pump runs when it should be off, swap `PUMP_ON` and `PUMP_OFF` in the code.

---

## 4. How to use the code: the 4 modes

At the top of `sketch_oct19a.ino`, set `#define MODE` and go through the modes **in order**:

### MODE 1: Check the wiring
Open the Serial Monitor (9600 baud). Touch the sensor or put it in soil; the raw numbers should change.

### MODE 2: Calibrate
- Sensor in **dry air**: copy the number into `RAW_DRY`
- Sensor in **a glass of water**: copy the number into `RAW_WET`
- Tank **almost empty**: copy the water level number into `WATER_LEVEL_MIN`

Upload again and check that the moisture goes from ~0 % (dry) to ~100 % (water).

### MODE 3: Pump test (choose ε)
With fairly dry soil, open **Tools → Serial Plotter**. The Arduino:
1. runs the pump for `TEST_PUMP_TIME` (3 s), reading M every second, and computes `V_MAX`
2. stops the pump and keeps reading for 2 minutes, and computes `RESIDUAL`
3. prints the minimum safe `EPS`

Press **RESET** to repeat. Take the **largest** value over a few runs and put it into `EPS`. This also answers the key physical question: **does M keep rising after the pump stops?**

### MODE 4: Run the automaton
The Arduino now waters automatically. In the Serial Plotter you'll see `M` bouncing between `M_LOW` and `M_STOP`, and `pump` (0 or 100) showing the state.

---

## 5. Settings you can change

| Variable         | Meaning                                               | Default |
|------------------|-------------------------------------------------------|---------|
| `M_REF`          | target moisture (%)                                   | `60`    |
| `EPS`            | stop margin (%), measure in MODE 3                    | `5`     |
| `M_STOP`         | pump ON → OFF threshold (`M_REF − EPS`)               | `55`    |
| `M_LOW`          | pump OFF → ON threshold                               | `45`    |
| `SAMPLE_ON`      | seconds between readings while watering               | `1`     |
| `SAMPLE_OFF`     | seconds between readings while drying                 | `60`    |
| `MAX_ON_TIME`    | safety: maximum pump ON time (s)                      | `30`    |
| `MIN_OFF_TIME`   | minimum OFF time so the water can spread (s)          | `300`   |

---

## 6. Safety features

- The pump is **OFF at startup**.
- **No pumping if the tank is empty**, so the pump never runs dry. The pump also stops immediately if the tank empties while it's ON.
- **`MAX_ON_TIME` safety timer**: if the sensor is disconnected or broken, it would never report "wet enough", so the pump is forced OFF after 30 s.
- **`MIN_OFF_TIME`**: after watering, the Arduino waits before deciding again, so the water has time to reach the sensor.
- The sensor is read **every second while the pump is ON**. You can't stop watching while watering.

---

## 7. Next steps

1. **Measure drying**: log `M` for a day with the pump OFF to find `d(M)` (often roughly `k·(M − M_eq)`).
2. **Check the properties experimentally**: does M always stay below `M_REF`? How many pump cycles happen per day?
3. **Compare** with the earlier approach (P controller on pump duration, `M[k+1] = M[k] + b·u[k]`, stable for `0 < Kp < 2/b`).
4. **Add temperature**: MAX6675 sensor, fan and heater, using the same automaton idea (heater OFF/ON with hysteresis).

---

## 8. References

- E. A. Lee & S. A. Seshia, *Introduction to Embedded Systems: A Cyber-Physical Systems Approach*: §4.1 Modal Models, §4.2 Classes of Hybrid Systems, Ch. 7 Sensors and Actuators.
- H. K. Khalil, *Control Systems: An Introduction* (2023): modeling the continuous dynamics, feedback, and actuator constraints (Ch. 4 §4-7).
