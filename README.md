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
| `attachIndex(pin, rising = true, pullUp = true, debounceUs = 0)` | Latch the position on an edge of any GPIO (Z phase, limit switch); `detachIndex()` to stop |
| `indexSeen()` / `indexCount()` / `lastIndexPosition()` | Index event status and the latched position |
| `zeroOnNextIndex(pos = 0)` / `zeroPending()` | One-shot homing on the next index event |

## Index input (Z phase / limit switch)

`attachIndex()` turns any GPIO into a position-latch input. The interrupt
handler only records a timestamp; the position at that instant is
reconstructed on the next read from the timestamp and the speed estimate,
so the latch does not race with the PIO reading and costs almost nothing.
Accuracy is dominated by interrupt latency (a few µs): even at 6000 rpm on a
400-step encoder that is only ~5 substeps of error.

- **Encoder Z/index phase**: `attachIndex(pin)` + `zeroOnNextIndex()` gives
  absolute position within one revolution after the first index. Comparing
  `lastIndexPosition()` between revolutions detects lost steps.
- **Limit switch on a linear axis**: `attachIndex(pin, false, true, 10000)`
  (falling edge, pull-up, 10ms debounce) + `zeroOnNextIndex()` homes the
  axis; every later hit re-latches, so drift can be monitored or corrected
  during reciprocating motion. Mechanical switches have hysteresis: home
  approaching from a consistent direction for best repeatability.

## When do you need phase calibration?

The optional calibration learns the four relative phase sizes of the
quadrature signal. Whether it helps depends on the encoder technology:

- **Optical incremental encoders** (e.g. the Nidec 24H's): phase asymmetry
  comes from the physical disc and sensor placement and is often a few
  percent. Calibration removes the resulting 4-per-step ripple in the speed
  estimate — worthwhile for smooth low-speed control, harmless to skip.
- **Magnetic encoders in incremental (ABI) mode** (AS5600, AS5047, ...):
  the AB signal is synthesized digitally from an angle measurement, so the
  phase sizes are already uniform and this calibration gains almost
  nothing. Their dominant error is once-per-revolution (magnet
  eccentricity), which a per-step model cannot correct — that would need a
  per-revolution correction referenced to the index (see docs/IDEAS.md).
- **Limit-switch homing**: calibration is irrelevant to the switch itself;
  switch hysteresis dominates repeatability.

Hence calibration is off by default; enable it only when speed ripple on an
optical encoder actually matters to you.

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

See [docs/IDEAS.md](docs/IDEAS.md) for the full list. Highlights:

- per-revolution error correction for magnetic encoders (index-referenced)
- cross-core safe snapshots
- `end()` releasing PIO resources
- Pico SDK build in CI

## License

BSD 2-clause, see [LICENSE](LICENSE). The PIO program is BSD-3-Clause,
Copyright Raspberry Pi (Trading) Ltd.
