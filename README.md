# Smart Plant Watering (Arduino)

An Arduino keeps a plant's soil at a target moisture. It reads a moisture sensor and decides **how many seconds to run the pump** using a proportional (P) controller.

This first version controls **moisture only**. Temperature (MAX6675), the fan and the heater come later.

---

## 1. The idea

The pump can only be **ON or OFF**, but a controller gives a continuous number. To connect the two, we use the controller's output as **pump ON time**:

Every period `T` (60 s), the Arduino:

1. reads the soil moisture `M` (in %)
2. computes the error `e = M_REF - M`
3. computes the pump time `u = Kp * e` (in seconds), limited to `0 ≤ u ≤ U_MAX`
4. turns the pump **ON for `u` seconds**, then **OFF** for the rest of the period

```
 |<------------------ T = 60 s ------------------>|
 [ pump ON: u s ][          pump OFF               ]
```

The controller is only computed at the start of each period, so the pump command is always clear (a new value never interrupts the current one).

---

## 2. The math model

### Plant model (difference equation)

```
M[k+1] = M[k] + b * u[k]
```

- `M[k]`: moisture at period `k` (%)
- `u[k]`: pump ON time during period `k` (s)
- `b`: moisture gained per second of pumping (%/s), **measured experimentally**

Transfer function (Z-transform):

```
G(z) = M(z) / U(z) = b / (z - 1)
```

This is a discrete **integrator**: when the pump runs, the moisture goes up, and in this simple model it never goes down on its own.

### Controller

```
e[k] = M_REF - M[k]
u[k] = clip(Kp * e[k], 0, U_MAX)
```

### Stability (ignoring saturation)

Put the controller into the model:

```
e[k+1] = (1 - b*Kp) * e[k]
```

The closed-loop pole is `z = 1 - b*Kp`. For stability it must be inside the unit circle:

```
|1 - b*Kp| < 1   =>   0 < Kp < 2/b
```

| `b*Kp`       | Behaviour                          |
|--------------|------------------------------------|
| 0 to 1       | smooth convergence (no overshoot)  |
| = 1          | reaches the target in one step     |
| 1 to 2       | converges but oscillates           |
| ≥ 2          | unstable                           |

We pick **`Kp = 0.5 / b`**, which gives smooth convergence.

> **Limit of this analysis:** the pump can't remove water (`u ≥ 0`). If the soil is too wet, the pump just stays OFF and we wait for the soil to dry. The stability result above holds for the linear model (no saturation) and for soil that starts drier than the target.

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

### MODE 3: Measure `b`
With fairly dry soil, the Arduino:
1. reads `M_before`
2. runs the pump for `TEST_PUMP_TIME` (3 s)
3. waits 60 s for the water to soak in
4. reads `M_after` and prints `b = (M_after - M_before) / TEST_PUMP_TIME`

Press **RESET** to repeat. Do it about 3 times, average the results and put the value into `b`.

### MODE 4: Run the controller
The Arduino now waters automatically. Open **Tools → Serial Plotter** to see `M`, `M_REF` and `u` live.

---

## 5. Settings you can change

| Variable          | Meaning                                  | Default     |
|-------------------|------------------------------------------|-------------|
| `M_REF`           | target moisture (%)                      | `60`        |
| `b`               | % gained per second of pumping           | `2.0` (measure it!) |
| `Kp`              | controller gain (s per %)                | `0.5 / b`   |
| `T_PERIOD`        | sampling period (s)                      | `60`        |
| `U_MAX`           | max pump time per period (s)             | `5`         |
| `TEST_PUMP_TIME`  | pump time for the MODE 3 test (s)        | `3`         |

---

## 6. Safety features

- The pump is **OFF at startup**.
- **No pumping if the tank is empty**, so the pump never runs dry.
- Pump time is capped at **`U_MAX`** each period, so the plant can't be flooded.
- Negative pump time is impossible: too wet means the pump stays OFF.

---

## 7. Next steps

1. **Measure drying**: log `M` for a day with the pump OFF and fit a better model:
   ```
   M[k+1] = a*M[k] + b*u[k] + c        (0 < a < 1)
   ```
   Stability then becomes `|a - b*Kp| < 1`.
2. **Test the theory**: try several `Kp` values and check what happens near `2/b`.
3. **Add an integral term (PI)** so the moisture really reaches `M_REF` even while the soil keeps drying (with anti-windup because of saturation).
4. **Add temperature**: MAX6675 sensor, fan and heater (model: `G(s) = R / (RCs + 1)`).

---

## 8. References

- E. A. Lee & S. A. Seshia, *Introduction to Embedded Systems: A Cyber-Physical Systems Approach*: hybrid systems (Ch. 4), sensors and actuators (Ch. 7).
- H. K. Khalil, *Control Systems: An Introduction* (2023): digital control and stability (Ch. 9), actuator constraints (Ch. 4 §4-7).
