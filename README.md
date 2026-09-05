English | [日本語](./README.ja.md)

[![CI](https://github.com/Suzu-Gears/Pico_PIO_Encoder/actions/workflows/ci.yml/badge.svg)](https://github.com/Suzu-Gears/Pico_PIO_Encoder/actions/workflows/ci.yml)

# Pico_PIO_Encoder

PIO encoder library for RP2040 / RP235X, for **Arduino (arduino-pico) and Pico SDK**.
Read measured position, estimate velocity on its own schedule, and use consecutive
or separated A/B GPIOs through explicitly named initialization methods. No DMA is used.

RP235X here means RP2350A/B and RP2354A/B. See the
[Raspberry Pi chip family](https://www.raspberrypi.com/products/rp2350/) and the
[validation scope](#validation-and-continuous-integration). SDK identifiers and
specific tested chip names remain `RP2350`; this family label does not imply
hardware qualification of every variant.

**Version 0.4.0** · [日本語](README.ja.md) · [Changelog](CHANGELOG.md) · [Release guide](docs/RELEASING.md)

A PIO program counts encoder steps **and measures transition timing entirely
in hardware**. The library combines both into a substep-interpolated position
and a fractional speed estimate for low count rates — where
naive "count steps per interval" methods only see 0, 1 or 2 counts of noise.

Based on [PicoEncoder](https://github.com/pmarques-dev/PicoEncoder)
by Paulo Marques, Pedro Pereira and Paulo Costa (BSD 2-clause). See [third-party notices](THIRD_PARTY_NOTICES.md) for license scope and upstream attribution. The PIO
program is retained for consecutive pins; nonconsecutive pins use a derived two-SM
program. The estimation and acquisition layers have been separated and extended.

Start with [wiring](#choose-the-initialization-that-matches-your-wiring),
[Arduino installation](#installation-and-compatibility), or
[Pico SDK setup](#usage-pico-sdk). See [angles and zeroing](#angles-speed-units-and-zeroing)
and [timing limits](#timing-and-resource-limits) when configuring your application.

## Choose the initialization that matches your wiring

**Check where A and B are connected, then explicitly select one of these calls. There is no automatic mode switching.**
Use GPIO numbers, not physical header-pin numbers.

### Wiring to consecutive GPIOs

Example: A to GPIO2, B to GPIO3. The GPIO numbers must differ by exactly one.

```cpp
const int result = encoder.beginConsecutive(2, 3);
if (result != 0) {
  Serial.println(Pico_PIO_Encoder::beginErrorMessage(result));
  while (true) delay(1000);
}
```

Uses 1 SM per encoder: **up to 4 encoders per PIO**, with a 13-clock input-check loop.
`beginConsecutive(2, 10)` returns an initialization error; it does not select another mode.
Example: [position_speed](examples/position_speed/position_speed.ino).

### Wiring to nonconsecutive GPIOs

Example: A to GPIO2, B to GPIO10.

```cpp
const int result = encoder.beginNonConsecutive(2, 10);
if (result != 0) {
  Serial.println(Pico_PIO_Encoder::beginErrorMessage(result));
  while (true) delay(1000);
}
```

Uses 2 SMs per encoder: **up to 2 encoders per PIO**, with a 17-clock input-check loop.
Adjacent pins are also accepted in this mode, but still cost 2 SMs.
Example: [nonconsecutive](examples/nonconsecutive/nonconsecutive.ino).

### Shared API after initialization

Both modes use the same acquisition, cached Snapshot, 64-bit position, velocity scheduling,
interpolation, calibration, index, unit conversion and lifecycle methods.
Swapping the A/B arguments reverses the sign consistently in both modes.

| Wiring / mode | Initialization | Encoders per PIO |
|---|---|---:|
| Adjacent GPIOs; prioritize capacity | `beginConsecutive(A, B)` | 4 |
| Separated GPIOs also supported | `beginNonConsecutive(A, B)` | 2 |

One PIO block hosts one mode. Different blocks may use different modes.
The library normally finds compatible free resources; force placement with, for example,
`beginConsecutive(2, 3, pio0)` and `beginNonConsecutive(6, 10, pio1)`.
Both programs use all 32 instructions, excluding other programs from that block.

**Always check initialization results. Do not run measurements after initialization fails.**

| Result | What to check |
|---|---|
| 0 | Success |
| −1 | No resources for the mode and GPIO window. Check encoder count, other PIO libraries and forced placement |
| −2 | Duplicate/out-of-range pins, nonadjacent pins passed to consecutive mode, invalid PIO, or repeated initialization |
| −3 | Initial PIO acquisition failed; resources have been released |

Snapshot fields `mode`, `pin_a`, `pin_b` and `state_machines` report the selected configuration.
Nonconsecutive mode also has higher acquisition work, different timing/skew and input-rate limits.
It does not simultaneously latch A/B, and invalid two-bit transitions are not handled identically.
See [mode validation](docs/PIN_MODES_VALIDATION.md) and [timing limits](#timing-and-resource-limits).

## Installation and compatibility

Use the [arduino-pico core by Earle F. Philhower](https://github.com/earlephilhower/arduino-pico).
The tested Arduino core is **6.0.0**. The architecture name is `rp2040`, including
its RP235X boards. Arduino's Mbed RP2040 core, AVR, ESP32 and generic Arduino
cores are not supported. Standalone **Pico SDK 2.1.1** builds and RP2040-Zero
hardware tests pass using the same library sources, without Arduino dependencies.
The SDK example also builds for Pico 2 (RP2350 ARM); RP2350 hardware remains
unverified. See the [SDK validation record](docs/PICO_SDK_VALIDATION.md).

1. Add the arduino-pico package index in Arduino IDE's Additional Boards Manager URLs:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`.
2. Install **Raspberry Pi Pico/RP2040/RP2350** in Boards Manager and select your board.
3. Download `Pico_PIO_Encoder-0.4.0.zip` from [Releases](https://github.com/Suzu-Gears/Pico_PIO_Encoder/releases),
   then use **Sketch > Include Library > Add .ZIP Library**.
   Alternatively, clone this repository into your sketchbook's `libraries/Pico_PIO_Encoder`.
4. Open **File > Examples > Pico_PIO_Encoder > position_speed** and set the A/B pins.

No separately installed Arduino library is required. `Wire` is supplied by the
board core and is only used by the optional Nidec/INA219 bench examples.

Connect A/B to the GPIO inputs selected above and share ground. Inputs must be 3.3V-compatible. Do not connect a 5V push-pull output
directly to the MCU. Encoders sharing a PIO block must fit within its common
32-GPIO window; see [Timing and resource limits](#timing-and-resource-limits).

An open-collector output only pulls its signal low. Connect each A/B signal
line through its own pull-up resistor to 3.3V so that the line goes high when
the output turns off. Both initializers use the pins as inputs.
**Omitting the pull argument enables internal pull-ups (`Pull::Up`).**
Use `using Pull = Pico_PIO_Encoder::Pull;` for the short names below.

| Call | A/B input configuration |
|---|---|
| `encoder.beginConsecutive(2, 3)` | Internal pull-ups enabled (default) |
| `encoder.beginConsecutive(2, 3, Pull::Up)` | Internal pull-ups enabled |
| `encoder.beginConsecutive(2, 3, Pull::None)` | Both internal pull-ups and pull-downs disabled |
| `encoder.beginConsecutive(2, 3, Pull::Down)` | Internal pull-downs enabled |

The same choices apply to `beginNonConsecutive(A, B, pull)`. To specify a PIO,
place it before the pull argument: `encoder.beginNonConsecutive(2, 10, pio0, Pull::None)`.
One choice applies to both A/B pins of that encoder. After `end()`, the next
initialization replaces both pull settings. The optional index input has its own
selection: `encoder.attachIndex(4, false, Pull::Up, 10000)`; its default is also `Pull::Up`.

Use `Pull::None` when external resistors or an actively driven output provide the bias. Whether the
internal pull-ups alone are sufficient depends on signal frequency and wiring
capacitance. Choose external resistor values to suit the encoder's output
specification and required rise time. Select `Pull::Down` only for a circuit
that requires a low bias; it does not replace the pull-up of an NPN open-collector output.

**RP2350 A2 — GPIO erratum E9:** input leakage can prevent an internal pull-down
from returning the signal low. PIO inputs are affected too, including with
`Pull::None`. At 3.3V I/O, the official remedy is a sufficiently strong low-driving
output or an external pull-down of 8.2kΩ or less. Internal pull-ups work, but the
encoder output must still drive a valid low. E9 is fixed in silicon steppings
A3/A4; these stepping names are separate from the A/B package suffixes.
See the [official erratum and revision history](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
The library configures the requested pulls; it does not correct this electrical fault.

## Highlights

- PIO counts quadrature edges without CPU intervention.
- `position()` returns an independent **64-bit measured count**.
- `positionSubsteps()` returns a **model-based interpolated position**, at
  64 substeps per quadrature step. This is not 64x measured resolution.
- `setSpeedUpdateIntervalUs()` sets the velocity estimator's own update
  interval. Intermediate position reads do not advance its averaging anchors.
- Stop detection uses the latest observed transition age and runs on each
  acquisition, including between velocity updates.
- `latest()` copies a saved Snapshot safely across cores without acquiring PIO data.
- Initialization, velocity readiness, freshness and saturation are explicit flags.
- `end()` releases PIO/index resources; reinitialization is supported.
- Arduino (arduino-pico) and Pico SDK use the same sources.
- Host tests cover independent schedules, acceleration, startup phase,
  stop/restart, low speed, reversal, counter/time wrap and index bookkeeping.
- Optional phase calibration is fed by acquisitions, not velocity updates.

## Separate position and velocity rates

```cpp
encoder.beginConsecutive(2, 3);
encoder.setMinRefreshIntervalUs(0);    // each read acquires a fresh sample
encoder.setSpeedUpdateIntervalUs(10000); // speed every 10ms
// Call from your 1kHz loop:
auto s = encoder.read();
// s.position: measured steps at s.timestamp_us
// s.position_substeps: interpolated position at that same acquisition
// s.speed: held velocity, computed at s.speed_timestamp_us
```

The interval defaults to **0** (recompute on every acquisition). There is no
background timer: a due update runs at the next read/refresh and a missed
update is not replayed. Reading at 1Hz cannot produce a 100Hz estimate.
`setMinRefreshIntervalUs()` controls hardware acquisition caching;
`setSpeedUpdateIntervalUs()` controls the separate velocity estimation interval.
Use `read()` once per control tick when timing matters. See
[examples/multirate](examples/multirate/) for 1kHz acquisition and 100Hz logging.

Velocity uses transition-to-transition differences over its own observations,
with step-boundary constraints for startup and deceleration. The nominal
interval is not a fixed-length moving average or a configurable low-pass filter:
at low speed, the transition interval can be much longer than the update period.
Speed is held between updates, except that observed idle timeout clears it
immediately. A direction change affects measured position at the next acquisition;
velocity may take until its next scheduled update to change sign.

### Counting and period measurement

The estimator uses **both count differences and edge timing**. It does not simply
divide the counts in a fixed sampling window by that window's length. With uniform
phase spacing, its transition-based estimate is:

`speed [steps/s] = count difference / elapsed time between the corresponding observed edges`

At low edge rates, a difference of one step uses the measured interval between
edges. When several steps pass between observations, their accumulated difference
is divided by the corresponding edge-time difference. Both cases use the same
calculation; there is no speed threshold that switches between a counting mode
and a period mode. Optional phase calibration adjusts the boundary positions.
The no-new-edge bounds and idle timeout described below also affect the result.

PIO retains the current count and transition age, not a timestamp queue for every
edge. Recomputing speed faster does not add new encoder observations.

### Choosing acquisition and velocity intervals

Choose position acquisition to match the application's control loop, then choose
velocity cadence for the required response, encoder resolution, speed and CPU load.
The following are starting points, not guaranteed controller performance.

| Use | Position acquisition | Velocity update | Rationale |
|---|---|---|---|
| Initial motor evaluation | 1ms (1kHz) | 5–20ms (50–200Hz) | Tested combinations; compare from a 10ms starting point |
| Prioritize response | Application control period | Every acquisition, setting 0 | Reduces publication delay; at a 1ms control period both update every 1ms |
| Display/logging | Retain the control acquisition rate | Retain the control setting | Read cached data separately, for example every 20–100ms |
| Very low speed | Required position response | Tune for edge spacing and response | Also set idle timeout above the longest expected edge gap |

For `C` quadrature counts/revolution and speed magnitude `n` rpm, average edge rate
is `C*n/60` steps/s and edge spacing is `60/(C*n)` seconds for nonzero speed. Set
idle timeout with margin above the longest expected interval, accounting for
phase-width variation and changing speed. At 400 counts/rev and 1rpm the mean
interval alone is 150ms, so the default 50ms timeout is too short.

Set maximum sample age above the ordinary acquisition interval and tolerated
jitter, and measure the complete loop including communication/logging. A 3ms
freshness limit for 1ms acquisition is one example, not a universal deadline.
Use `UpdateLate`, acquisition timestamps and the examples' timing counters.

`refresh()` always acquires; self-refreshing getters respect the acquisition
cache. Set `setMinRefreshIntervalUs(0)` for a fresh `read()` every call. Velocity
setting 0 recomputes on every acquisition; nonzero settings run at the first
acquisition after the deadline. There is no background timer and no velocity
update between acquisitions.

Three rates differ: physical A/B edges, CPU acquisition, and velocity computation.
At 400 steps/rev and 30rpm there are only 200 edges/s, averaging one edge every
5ms. A naive 1ms count difference therefore gives mostly 0step/s and occasional
1000step/s, although the motor is moving steadily at 200step/s. Recomputing faster
does not create additional sensor observations or guarantee equivalent control bandwidth.

PIO continuously decodes A/B and maintains a **32-bit hardware counter** plus time
since the latest transition, without per-edge CPU interrupts. The CPU reads a fresh
count/time pair, accumulates the count difference into **64-bit measured position**,
estimates velocity and publishes a Snapshot. PIO keeps counting between reads,
subject to the documented wrap/gap limits; the cached Snapshot stays unchanged.
The protocol retains cumulative state, not the complete history of every edge.

The CPU reconstructs transition time from acquisition time and the elapsed PIO
loop count (13 system-clock cycles per consecutive loop, 17 per nonconsecutive loop). On a velocity update it divides
the change in observed transition-boundary position by the change in transition
time. One step over 5ms gives 200step/s even if acquisition is much faster. Multiple
steps between observations and calibrated phase widths are handled by the same idea.
This is an average over transition observations, not directly measured instantaneous speed.

Boundary constraints also use the absence of a new edge. In a forward, equal-phase
example, no next edge after 6ms is inconsistent with a constant 200step/s model:
that model would already have moved 1.2 steps. The bound instead falls to about
1/0.006 = 167step/s. The implementation handles both directions and selects
transition/sample intervals to avoid very short divisors where possible. Idle
timeout eventually sets speed to zero at an acquisition. Startup/reversal use
the warming-up status until enough observations are available. These constraints
do not reconstruct arbitrary acceleration or hidden out-and-back motion.

Stop detection uses elapsed time since the observed transition, independently
of acquisition and velocity cadence. A sample-count criterion would change its
effective timeout with the acquisition rate: three unchanged reads correspond
to roughly 3ms at 1kHz or 30ms at 100Hz. Elapsed-time detection avoids this
dependency. It is evaluated on acquisitions, including between velocity ticks.

A slower velocity interval reduces computation and can span more transitions,
but adds update latency. It is neither a fixed-duration averaging window nor an
LPF bandwidth setting. Interpolated substep position projects from the latest
boundary with the held velocity and clamps to the current phase interval. Measured
position is accumulated from hardware counts and never derived by integrating velocity.

See the [Japanese illustrated explanation](README.ja.md) and the implementation:
[PIO](src/substep_encoder.pio), [acquisition](src/Pico_PIO_Encoder.cpp),
[motion estimator](src/substep_encoder_motion.h).

### Position and Snapshot types

`position()` and `Snapshot.position` are measured quadrature counts (`int64_t`).
Interpolation is available separately through `positionSubsteps()` and the angle
helpers. `speed()` is fractional `float` step/s.
`resetPosition()` first acquires hardware.

Snapshot contains status, sequence, a 64-bit acquisition timestamp and
its freshness limit. Its binary layout is not a serialization format.
Check `positionValid()` / `speedValid()` before using values in a controller.
Only `latest()` is intended for another core. Self-refreshing getters
are owner-context APIs. Copy/move of the encoder object is disabled.

See [Snapshot, diagnostics and lifecycle](docs/SNAPSHOT_LIFECYCLE_JA.md) for the
full contract, including configuration preserved across `end()` and reinitialization.

## Usage (Arduino)

```cpp
#include <Pico_PIO_Encoder.h>

Pico_PIO_Encoder encoder;

void setup() {
  if (encoder.beginConsecutive(2, 3) != 0) { while (true) delay(1000); }
}

void loop() {
  int64_t pos = encoder.position();  // quadrature steps (4x counting)
  float spd = encoder.speed();       // steps per second (fractional)

  // or, for a guaranteed-consistent pair:
  Pico_PIO_Encoder::Snapshot s = encoder.read();

  // full 1/64-step resolution when you need it:
  int64_t pos_ss = encoder.positionSubsteps();
  int32_t spd_ss = encoder.speedSubsteps();
}
```

Default units are quadrature steps: a 100 PPR encoder counted 4x gives
`400` steps per revolution. The estimator uses substeps
(1/64 step, `25600` per revolution for the same encoder) — that is where
the smooth low-speed estimates come from — and the `*Substeps()` getters
expose that full resolution. `speed()` is fractional (float) so nothing is
lost at low speed even in step units.

For angle within a revolution, the wrapped helpers take the modulo in integer
substeps before converting to float. This avoids loss of angular precision caused
by converting a large cumulative position directly to float. The result still has
ordinary floating-point rounding. Use `turns()` alongside the wrapped angle when
you need to represent multiple revolutions; acquire one Snapshot when both values
must correspond to exactly the same observation.

## Angles, speed units and zeroing

Configure the number of **quadrature counts per revolution**, not just the
encoder's pulse count. A 100 PPR encoder counted four times gives 400 steps/rev.

```cpp
encoder.setStepsPerRev(400);
float degrees = encoder.angleInRevDeg();
float radians = encoder.angleInRevRad();
float velocity = encoder.radPerSec();
```

| Method | Unit / range |
|---|---|
| `angleInRevDeg()` | Angle within one revolution, nominally [0, 360) degrees |
| `angleInRevRad()` | Angle within one revolution, nominally [0, 2π) radians |
| `angleRad()` | Signed cumulative angle, radians, `double` |
| `revolutions()` | Signed cumulative revolutions including the fractional turn, `double` |
| `turns()` | Floor of cumulative revolutions, `int64_t` |
| `rpm()` / `revPerSec()` / `radPerSec()` | Signed velocity in rpm / revolutions per second / radians per second, `float` |

Angle helpers convert **interpolated position**; `position()` remains the measured
integer count. To convert measured position only, use `position() * (360.0 / 400)`
for degrees in this example. Unit helpers return zero until `setStepsPerRev()`
is configured. Floating-point conversion can round a wrapped angle at its boundary.

```cpp
encoder.resetPosition();     // set the current position to zero
encoder.resetPosition(100);  // alternatively assign 100 measured steps here
```

`resetPosition()` acquires a sample before changing the coordinate reference.
It also changes the origin used by angle and revolution helpers; it does not
move the motor. No extra input is needed for this operation. Absolute mechanical
orientation after power-up requires an external reference or a known starting
position. Optional input-based homing uses [`attachIndex()`](#index-input-z-phase--limit-switch).

These getters and zeroing operations belong to the acquisition owner. For several
values from one observation, keep one `read()` Snapshot and convert its fields;
the individual getters above can acquire at different times. Check sample validity
before using the result. A failed acquisition prevents `resetPosition()` from
changing the reference.

## Usage (Pico SDK)

Set `PICO_SDK_PATH` to your SDK checkout and configure its C/C++ toolchain.
In an SDK project, after `pico_sdk_init()`:

```cmake
add_subdirectory(path/to/Pico_PIO_Encoder)
target_link_libraries(your_app pico_pio_encoder)
```

See [pico_sdk_example/](pico_sdk_example/) for a complete project. With the SDK
and its toolchain configured, build from the library root:

```sh
cmake -S pico_sdk_example -B build-sdk -DPICO_BOARD=waveshare_rp2040_zero
cmake --build build-sdk
```

Use `pico2` for a Pico 2 ARM build. The API and both wiring modes are shared
with Arduino. [Verified versions and hardware scope](docs/PICO_SDK_VALIDATION.md).
The build produces `position_speed.uf2` for consecutive GPIO2/3 and
`nonconsecutive.uf2` for GPIO2/10. Choose the example matching your wiring and
edit the pins before flashing. Both examples use USB standard output and leave
motor control to the application.

## Cached Snapshot and diagnostics

The owner calls `refresh()` or `read()`. Another core only calls `latest()`:

```cpp
const auto sample = encoder.latest(); // saved data; no hardware acquisition
if (sample.positionValid() && sample.speedValid()) {
  // Log/use this coherent pair of values.
}
```

`timestamp_us` and `timestamp_us64` identify the same acquisition; the speed has
its own `speed_timestamp_us`. `sequence` increments on publication and wraps as
uint32_t. Do not treat it as an indefinitely unique identifier.

| Status flag | Meaning |
|---|---|
| `Initialized` | A successful begin has not yet been ended |
| `VelocityWarmingUp` | Startup, restart or reversal needs more velocity observations |
| `VelocityValid` | Sufficient velocity observations, or confirmed stopped zero speed |
| `Stale` | At read time, the saved acquisition exceeds `max_age_us` |
| `UpdateLate` | The most recent acquisition gap exceeded the configured limit |
| `SpeedSaturated` | The held velocity reached the internal fixed-point limit |
| `ReadFailed` / `PioFault` | Held data after an acquisition failure; PioFault requires end/reinitialization. Successful foreground acquisition clears ReadFailed. |
| `PositionSaturated` | Position arithmetic saturated; reset the reference or reinitialize |

Use `sample.has(Pico_PIO_Encoder::Stale)` to inspect a flag. Position validity
requires initialization and no stale, position-saturated, read-failed or PIO-fault status. Speed validity
additionally requires velocity readiness and no speed saturation. A late flag
may remain on a fresh resumed sample; it clears on the next timely acquisition.
These flags do not detect every disconnected encoder, glitch or missed edge.

All hardware/configuration/lifecycle operations have one owner context.
Initialization, end and index attach/detach are foreground operations. A cached
copy uses a short IRQ-safe spinlock; the other core must stop reading before the
object is destroyed. See [multicore_snapshot](examples/multicore_snapshot/) and the
[detailed lifecycle contract](docs/SNAPSHOT_LIFECYCLE_JA.md).

## API

| Method | Description |
|--------|-------------|
| `beginConsecutive(A, B, pull = Pull::Up)` / `beginNonConsecutive(A, B, pull = Pull::Up)` | Explicit mode selection; optional PIO precedes pull. Invalid pull values return -2. |
| `position()` | Measured quadrature count, `int64_t` |
| `speed()` | Speed in steps per second, `float` (fractional) |
| `positionSubsteps()` / `speedSubsteps()` | Full 1/64-step resolution (`int64_t` substeps, `int32_t` substeps/s) |
| `stopped()` | True when no transition arrived within the idle timeout |
| `read()` | Measured count, held speed, acquisition and speed timestamps, interpolated substeps, stopped flag |
| `latest()` / `initialized()` | Read cached data / initialization state safely on either core |
| `end()` | Release PIO/index resources; false on the wrong owner core |
| `setMaxSampleAgeUs(us)` | Freshness and acquisition deadline limit; default 10000µs, 0 disables |
| `refresh()` | Force a hardware re-read and publish a Snapshot now |
| `resetPosition(to = 0)` | Set the current position in steps |
| `setMinRefreshIntervalUs(us)` | Getters re-read the hardware when the last reading is older than this (default 100µs) |
| `setSpeedUpdateIntervalUs(us)` | Velocity update interval; 0 = every acquisition (default) |
| `setIdleTimeoutUs(us)` | No transition for this long ⇒ speed snaps to 0 (default 50ms, see below) |
| `enableAutoCalibration()` / `calibrationReady()` / `getPhases()` / `setPhases(p)` | Optional phase-size calibration |
| `attachIndex(pin, rising = true, pull = Pull::Up, debounceUs = 0)` | Capture PIO count in the GPIO IRQ (Z/limit input below GPIO32); invalid pull values return -2; `detachIndex()` to stop |
| `indexSeen()` / `indexCount()` / `lastIndexPosition()` | Index event status and the latched position |
| `zeroOnNextIndex(pos = 0)` / `zeroPending()` | One-shot homing on the next index event |
| `lastIndexSpacing()` | Distance in steps between the two most recent index events — spacing can help diagnose count errors but also includes capture latency |
| `setStepsPerRev(steps)` then `revolutions()` / `angleRad()` / `revPerSec()` / `rpm()` / `radPerSec()` | Optional unit helpers (steps = PPR × 4). Cumulative position uses double; velocity uses float. |
| `angleInRevRad()` / `angleInRevDeg()` / `positionInRevSubsteps()` / `turns()` | Wrapped angle within one revolution + floor of cumulative turns. Integer modulo before float conversion avoids precision loss from accumulated travel. |

## Index input (Z phase / limit switch)

`attachIndex()` reads the PIO count in the GPIO ISR. It does not reconstruct
past position from the current speed, so slowing/stopping before the next
foreground read does not corrupt the captured count. The ISR retains the
most recent two event counts and the first event after `zeroOnNextIndex()`.
`indexCount()` counts accepted events, even if several arrive between reads.
`lastIndexSpacing()` uses the most recent two actual events in either direction.

This is a **software capture with one-step quantization**, not a hardware latch
synchronous with the Z edge. Interrupt latency plus FIFO read latency still
cause error at high speeds. No substep accuracy is claimed. Homing shifts the
measured count reference; the interpolated angle can differ by a fraction of
a step. Delayed polling does not add constant-speed extrapolation error.

Call hardware/estimator methods on an indexed instance from its IRQ core; `latest()` may be used on another core. All index pins
managed by this library must use that same core; initialization checks this.
The FIFO transaction is protected against an index ISR splitting a pair.
Registering multiple index pins updates the entire raw IRQ mask, and detach
releases that mask so the SDK/Arduino default callback can use the pin again.

For a Z input, use `attachIndex(pin)` and `zeroOnNextIndex()`. For a limit switch,
use falling edge, pull-up, and a debounce interval appropriate to the switch.
GPIOs already used by another component must not be shared with the index input.
## When do you need phase calibration?

Calibration learns unequal widths of the four quadrature phases. It can reduce
phase-related velocity ripple when motion is steady and acquisition is fast
enough to observe every step. It does not correct mechanical eccentricity,
missing edges, signal noise or once-per-revolution error. It is off by default.
Only enable it if measured ripple and phase asymmetry justify it. Save and restore
`getPhases()` / `setPhases()` if you want to reuse learned phase widths.

## Timing and resource limits

- A block has four SMs: up to four consecutive or two nonconsecutive encoders, all using the same mode and 32-GPIO window.
  On the 48-GPIO RP2350B / RP2354B, hardware can select GPIO0–31 or GPIO16–47
  per PIO block. All A/B pins assigned to that block must fit within the same
  window; the window cannot be selected independently per encoder. See the
  [Raspberry Pi datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
  and [Pico SDK PIO GPIO configuration](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_pio/include/hardware/pio.h).
- On RP2350B, a free block selects a suitable GPIO window. Occupied blocks never change window; the original window is restored after final release. A pair such as GPIO15/32 fits neither window and is rejected. Upper GPIOs have not been hardware-tested. Select GPIOs available on your chip and board.
- Each 32-instruction PIO program occupies a whole block. Encoders of the same mode share
  a block. `end()` releases its SM(s) for reuse; the last encoder returns the whole
  block and program memory. Destroy on the owner core after all readers stop.
- Keep acquisitions less than 2^31 microseconds apart and fewer than 2^31 net
  steps apart. The velocity estimator's observations must be fewer than 2^25
  steps apart because its calibrated boundary coordinates are modulo 2^32
  substeps. 64-bit accumulation removes long-run counter wrap, not these
  maximum-gap constraints. Avoid changing the system clock after initialization.
- PIO encodes edge age and direction in one counter; age becomes ambiguous
  after 2^31 PIO loops (about 223 seconds for consecutive mode, 292 seconds for nonconsecutive mode, at 125MHz). Keep acquisition gaps and
  the idle timeout well below that. An already observed stopped state remains
  stopped until the measured count changes, including across timer wrap.
- Default idle timeout 50ms intermittently reports stopped below roughly
  20 steps/s. Raise it for slower motion; this also delays stop detection.
- Intermediate reads leave velocity anchors unchanged. Motion that reverses
  and returns to the same count between reads cannot be reliably distinguished
  from no movement with the retained PIO protocol. Exact arbitrary-motion
  invariance across different acquisition schedules is not promised.
- Phase calibration requires acquisitions fast enough to observe every step
  and steady movement. Lowering only the velocity update rate does not reduce
  calibration acquisition frequency. Disable calibration when comparing rates.
- One control core/context owns hardware access and settings. Other cores may
  call `latest()` to read a coherent cached Snapshot. See
  [Snapshot, diagnostics and lifecycle](docs/SNAPSHOT_LIFECYCLE_JA.md).
- Index captures must be processed within 2^31 steps of the captured event;
  adjacent index events also must be less than 2^31 steps apart.
- Input bounce/glitches are not digitally filtered. The index input supports
  software debounce. Validate signal quality and count accuracy on hardware.

RP2040-Zero / Nidec hardware tests passed for 1kHz acquisition, 5/10/20ms
velocity updates, low speed, reversal, and stop across a read gap. See the
[measured results](docs/VALIDATION_RESULTS.md), Japanese
[handoff procedure](docs/HARDWARE_VALIDATION_JA.md), and
[live bench protocol](docs/BENCH_PROTOCOL_JA.md).
## Differences from PicoEncoder

| | PicoEncoder | Pico_PIO_Encoder |
|--|-------------|----------------|
| Read contract | call `update()` once per control loop | getters acquire on demand; velocity interval is separate |
| Stop detection | after 3 samples with no step (rate-dependent) | wall-clock timeout (rate-independent) |
| Position | 32-bit substeps (wraps) | 64-bit measured count + separate interpolation |
| Calibration | separate high-frequency call | piggybacked on reads, opt-in flag |
| PIO placement | pio0/pio1 automatic | + RP2350 pio2, + explicit `beginConsecutive(A, B, pio)` |
| Cores | Arduino (mbed + arduino-pico) | arduino-pico + plain Pico SDK |
| Tests | — | host-side unit tests |

## Examples and support

| Example | Purpose |
|---|---|
| `position_speed` | Basic measured count, interpolated position and speed |
| `nonconsecutive` | Explicit separated A/B inputs and initialization diagnostics |
| `multirate` | 1kHz acquisition and 5/10/20ms velocity settings, timing counters |
| `multicore_snapshot` | Control-core acquisition and cached logging on core 1 |
| `auto_calibration` | Optional phase learning |
| `index_homing` | Optional Z/home capture; extra input required |
| `nidec_wiring_check` | Specific Nidec/INA219 fixture, starts braked |
| `nidec_bench` | Commanded motor bench; read its wiring and command protocol first |

Report bugs through [GitHub Issues](https://github.com/Suzu-Gears/Pico_PIO_Encoder/issues).
Include board/core/library versions, pin mapping, acquisition/velocity/idle settings,
Snapshot flags and a minimal reproducer. See [Contributing](CONTRIBUTING.md).
Maintainer: Ryota SUZUKI, `suzuki.ryota.ua@tut.jp`.

## Validation and continuous integration

RP2040-Zero hardware checks cover both wiring modes, resource release/reuse,
independent velocity updates and cached multicore Snapshots. Arduino uses
arduino-pico 6.0.0; standalone SDK hardware checks use Pico SDK 2.1.1.
RP2350 has been compile-tested, including Arduino GPIO-window code paths;
RP2350 hardware and upper GPIOs remain unverified.

The [CI workflow](.github/workflows/ci.yml) runs on pushes, pull requests and manual
dispatch: host estimator tests, Arduino Lint, all Arduino examples on RP2040 and
RP2350, and both SDK examples on Pico / Pico 2 ARM. The core and SDK versions are
pinned to the tested releases. CI compilation does not replace hardware tests.

See [mode tests](docs/PIN_MODES_VALIDATION.md), [SDK tests](docs/PICO_SDK_VALIDATION.md)
and [release preparation checks](docs/RELEASE_VALIDATION.md) for results and limits.
The workflow checks code; it does not create releases or register the library in
Arduino Library Manager. Maintainers can follow the [release guide](docs/RELEASING.md).

## Future work

See [docs/IDEAS.md](docs/IDEAS.md) for the full list. Highlights:

- per-revolution error correction for magnetic encoders (index-referenced)
- RP2350 hardware testing, including upper GPIO windows on suitable boards

## License

BSD 2-clause, see [LICENSE](LICENSE). The PIO program is BSD-3-Clause,
Copyright Raspberry Pi (Trading) Ltd. Full upstream texts and file scope are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSES](LICENSES/).
