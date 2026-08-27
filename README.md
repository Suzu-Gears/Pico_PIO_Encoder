# SubstepEncoder

High resolution, zero-CPU-load quadrature encoder library for RP2040 / RP2350.

A PIO program counts encoder steps **and timestamps every transition entirely
in hardware**. The library combines both into a substep-interpolated position
and a speed estimate that stays accurate down to very low speeds — where
naive "count steps per interval" methods only see 0, 1 or 2 counts of noise.

Based on the excellent [PicoEncoder](https://github.com/pmarques-dev/PicoEncoder)
by Paulo Marques, Pedro Pereira and Paulo Costa (BSD 2-clause). The PIO
program and the core estimation idea are inherited unchanged; the surrounding
layer is redesigned (see "Differences from PicoEncoder" below).

## Highlights

- **No `update()` cadence to get right.** Getters refresh themselves from the
  hardware. Read at 10kHz or at 1Hz — position and speed are correct either
  way. (Stop detection is wall-clock based, not sample-count based, so
  results do not depend on how often you read.)
- **64-bit position** in substeps — never wraps in practice.
- **Zero CPU load** for the tracking itself; a read is a few microseconds of
  FIFO draining and integer math.
- **One repo, two build systems**: Arduino library (arduino-pico core) and
  Pico SDK library (`add_subdirectory` + `target_link_libraries`), same
  sources, one release.
- **Host-tested estimator**: the estimation logic is a pure C++ header,
  unit-tested on the host (`test/host/`), including a test proving 1kHz and
  100Hz sampling produce identical results.
- Optional **phase-size calibration**, piggybacked on normal reads.

## Usage (Arduino)

```cpp
#include <SubstepEncoder.h>

SubstepEncoder encoder;

void setup() {
  encoder.begin(2);  // phases on GPIO2 + GPIO3 (consecutive pins required)
}

void loop() {
  int64_t pos = encoder.position();  // substeps (64 per quadrature step)
  int32_t spd = encoder.speed();     // substeps per second

  // or, for a guaranteed-consistent pair:
  SubstepEncoder::Snapshot s = encoder.read();
}
```

For a 100 PPR encoder counted 4x there are `100 * 4 * 64 = 25600` substeps
per revolution.

## Usage (Pico SDK)

```cmake
add_subdirectory(path/to/SubstepEncoder)
target_link_libraries(your_app substep_encoder)
```

See [pico_sdk_example/](pico_sdk_example/) for a complete project.

## API

| Method | Description |
|--------|-------------|
| `begin(firstPin, pullUp = true)` | Start tracking. Phases on `firstPin` / `firstPin+1` (must be consecutive GPIOs). Returns 0 on success, -1 if no PIO block is free |
| `begin(firstPin, pio, pullUp)` | Same, but force a specific PIO block (useful alongside other PIO libraries) |
| `position()` | Position in substeps, `int64_t` |
| `speed()` | Speed in substeps per second, `int32_t` |
| `stopped()` | True when no transition arrived within the idle timeout |
| `read()` | `{position, speed, timestamp_us}` from one consistent reading |
| `refresh()` | Force a hardware re-read now |
| `resetPosition(to = 0)` | Set the current position |
| `setMinRefreshIntervalUs(us)` | Getters re-read the hardware when the last reading is older than this (default 100µs) |
| `setIdleTimeoutUs(us)` | No transition for this long ⇒ speed snaps to 0 (default 50ms, see below) |
| `enableAutoCalibration()` / `calibrationReady()` / `getPhases()` / `setPhases(p)` | Optional phase-size calibration |

## Notes

- **Consecutive pins**: the PIO requires the two phases on consecutive GPIOs;
  pass the lower-numbered one to `begin()`. Which wiring order maps to
  "positive" direction is up to your wiring — flip the sign in user code if
  needed.
- **PIO usage**: the program needs all 32 instructions of a PIO block, so the
  block is claimed exclusively (like PicoEncoder). Up to 4 encoders share one
  block; on RP2350 the third PIO is used too. If you run other whole-block
  PIO libraries (e.g. can2040-based CAN), use `begin(pin, pio1)` etc. to
  control placement.
- **Idle timeout**: while no transitions arrive, the speed estimate already
  decays toward zero on its own (bounded by the step boundaries); the timeout
  only snaps it to exactly 0. Default 50ms means speeds below ~20 transitions
  per second intermittently read as stopped — raise the timeout to track
  slower motion, lower it to detect stops faster.
- **Threading**: one instance must be read from one core at a time; wrap
  reads in your own lock if both cores need the same encoder.

## Differences from PicoEncoder

| | PicoEncoder | SubstepEncoder |
|--|-------------|----------------|
| Read contract | call `update()` once per control loop | getters self-refresh, any rate |
| Stop detection | after 3 samples with no step (rate-dependent) | wall-clock timeout (rate-independent) |
| Position | 32-bit substeps (wraps) | 64-bit substeps |
| Calibration | separate high-frequency call | piggybacked on reads, opt-in flag |
| PIO placement | pio0/pio1 automatic | + RP2350 pio2, + explicit `begin(pin, pio)` |
| Cores | Arduino (mbed + arduino-pico) | arduino-pico + plain Pico SDK |
| Tests | — | host-side unit tests |

## Roadmap

- Index (Z) pulse support for absolute-within-revolution positioning
  (`attachIndex(pin)`, GPIO-interrupt based)

## License

BSD 2-clause, see [LICENSE](LICENSE). The PIO program is BSD-3-Clause,
Copyright Raspberry Pi (Trading) Ltd.
