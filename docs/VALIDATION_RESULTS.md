# Validation record — 2026-09-05

Historical measurements below were recorded under the development name SubstepEncoder.
Current public identifiers are Pico_PIO_Encoder; raw measurement logs are preserved.
For renamed-source build checks, see [release validation](RELEASE_VALIDATION.md).

For the current 0.4.0 explicit wiring modes, see [PIN_MODES_VALIDATION.md](PIN_MODES_VALIDATION.md).
For standalone Pico SDK builds and hardware checks, see [PICO_SDK_VALIDATION.md](PICO_SDK_VALIDATION.md).
The records below describe earlier development checks; their limitations describe those earlier runs.

## Earlier 0.2.0 estimator checks

- Version 0.2.0: measured 64-bit count, separate interpolation, independently
  scheduled velocity, acquisition-based stop detection, captured-count index.
- Compiler: GCC / MinGW-w64, flags
  `-std=c++17 -O2 -Wall -Wextra -Werror -static`.
- **15 host test functions passed**, including deadline phase under µs jitter.
- Other cases: acceleration with independent 1kHz/100Hz schedules, initial
  phases/directions, sparse stop/restart, timeout overriding held speed,
  5steps/s motion, time/count wrap, reversal/zero, large count gaps, duplicate
  timestamps, timestamp jitter, multiple index events/homing/debounce.
- `git diff --check`: no whitespace errors at source installation.

## Builds

- Installed Arduino core: `rp2040:rp2040 6.0.0`.
- `position_speed`, `auto_calibration`, `index_homing`, `multirate`, and
  `nidec_wiring_check` compiled for `rp2040:rp2040:rpipico`.
- `nidec_wiring_check` and `nidec_bench` compiled for the actual
  `rp2040:rp2040:waveshare_rp2040_zero` board.
- The final deadline-phase correction was host-tested and compiled/run in
  `nidec_bench` on RP2040-Zero.
- Final bench build: 70,508 bytes flash, 9,636 bytes global RAM.
- Pico SDK standalone CMake build and RP2350 build: not run.

## Test fixture

RP2040-Zero, Nidec 24H motor, A28/B27, DIR26, PWM15 (active LOW), BRAKE14 (LOW brakes), INA219 SDA4/SCL5 at 0x40; 400 quadrature steps/revolution and no Z input. These are dated test records, not a claim about any connected device or current firmware.

## Physical tests and measured results

- INA219 bus voltage: **11,792–11,800mV**.
- Standby current indication: about 7.9mA using the original shunt conversion.
- 1kHz acquisition, USB logs enabled: **missed=0** in all captured runs.
- Largest observed read duration across runs: **119µs**; encoder/I²C/control
  work duration: **351µs**. Final firmware active runs reported 93µs / 337µs.
  The work timer excludes USB logging, while missed counts whole-loop overruns.
- All captured runs: **I²C errors=0, fault=0**.

Steady-window comparison (first 250ms and last 30ms of each drive removed):

| Mode / drive | Speed interval | Count difference / elapsed time (steps/s) | Mean estimated speed (steps/s) | Relative difference |
|---|---:|---:|---:|---:|
| PWM +5000 | 10ms | -2290.42 | -2289.50 | -0.04% |
| PWM +5000 | 20ms | -2289.18 | -2289.21 | 0.00% |
| PWM +5000 | 5ms | -2289.71 | -2289.31 | -0.02% |
| PWM -5000 | 10ms | +2292.54 | +2292.86 | +0.01% |
| PWM +1500 (low speed) | 10ms | -179.09 | -178.85 | -0.13% |
| PWM +2000 | 10ms | -491.01 | -491.86 | +0.17% |

At roughly 179steps/s (about 27rpm for 400steps/rev), the 1kHz acquisitions
had **zero stopped=true rows during the steady drive window**. This is the
low-speed region in which PicoEncoder's three-unchanged-sample rule was a problem.
PWM +1000 did not sustain rotation; it correctly reported stopped.

Current-feedback runs were +50mA, -50mA, +100mA targets for 1.5s each.
Mean count-rate versus estimated-speed differences in the trimmed windows
were -0.66%, -0.61%, -0.46%. Filtered current means were approximately +39.05,
-39.16, +68.81mA: these short runs had **not converged to the current targets**.
They validate encoder operation alongside the current loop, not PID tuning.
The bench uses the original P/I coefficients with measured dt, which differs
from the original example's fixed delta=0.01.

An acquisition-only pause of 200ms overlapped PWM motion and braking. The
last held count was -1506; after resumption the count was -1715 and speed=0,
stopped=true. Stop-detection timestamp 117835522µs identifies the first resumed
update; the next logged sample was 117842515µs. Motion during the pause was
retained. The motor timeout and I²C processing continued during the pause.

**Direction observation:** with A28/B27 and the same DIR convention as the
provided motor wrapper, positive PWM command produced negative raw count/speed,
and negative command produced positive raw count/speed. Pico_PIO_Encoder leaves
this raw polarity unchanged. A motor wrapper that requires positive command
and positive speed must apply the measured sign at its boundary. The subsequent 0.3.0 downstream integration test applied that correction in its motor wrapper.

These are consistency checks using the same A/B source; the percentage
figures are not independent absolute encoder accuracy measurements. No external
reference counter was attached. Maximum-output endurance, extremely slow
physical motion below 20steps/s, and Z/index hardware were not tested.

## Included artifacts

The [data directory](data/) contains the powered rate/low-speed/gap runs, 24V
maximum-speed run, numerical summaries, and 0.3.0 downstream integration logs.
CSV command schedules are included beside the relevant files. The bench sketch
and [host runner](../tools/run-nidec-bench.ps1) can produce new records.
The downstream integration fixture was part of the Nidec integration work;
its logs are evidence of that test, not a motor command protocol for this library.

## 24V maximum-speed test — 2026-09-05

INA219 was checked while stopped: 11.8V before adjustment and 23.996V after setting the supply to 24V.
The bench was extended with `R output ramp_ms hold_ms`, keeping the existing
1600mA cutoff and the 10-second total command limit. Library estimator code
was not changed for this test.

The firmware compiled for RP2040-Zero (70,772 bytes flash, 9,648 bytes global
RAM) and ran on the test fixture. First a +10000 command was ramped up/held/down
over 1/1/1 seconds, then **+32767** over **2/3/2 seconds**. Maximum command
means analogWrite=0, continuously LOW, for this negative-logic PWM motor.

| Measurement | Result |
|---|---:|
| Bus voltage sampled during full-output run | 24.040–24.076V |
| Steady estimated speed magnitude | 39,822.542 steps/s |
| Steady speed, 400 steps/rev | **5,973.38rpm** |
| Peak logged speed | **5,976.04rpm** |
| Steady count-difference/time speed | 39,822.378 steps/s |
| Difference between the two speed means | 0.0004% |
| Mean indicated current during steady full output | 83.40mA |
| Peak logged indicated current in the run | 155.30mA |
| Max encoder read duration since firmware start | 123µs |
| Max encoder/I²C/control work duration | 348µs |
| Missed 1kHz deadlines / I²C errors / faults | **0 / 0 / 0** |

Steady statistics use the full-output rows after the first second of the
3-second hold (192 logged rows). Full output was observed over 2994ms at the
approximately 100Hz log cadence. This is the maximum speed measured for this
motor/setup/direction at the supplied voltage, not an independently verified
tachometer reading or a long-duration endurance rating.

Current values use the existing 0.1ohm shunt conversion without independent
calibration. The sampled voltage/current maxima do not bound fast transients.
INA219's specified common-mode range ends at 26V; its 32V register range does
not increase the electrical input rating (TI datasheet:
https://www.ti.com/lit/ds/symlink/ina219.pdf).

Final state verified in telemetry: **mode S, output 0, speed 0, stopped=true;
brake ON**. This run used `nidec_bench` with its R command.

## 0.3.0 lifecycle, publication and downstream integration

- **16 host test functions passed**, including saturation and recovery, startup,
  stopped/restarted velocity readiness and reversal readiness.
- arduino-pico 6.0.0 / RP2040-Zero builds passed for the downstream validation,
  current-feedback, speed and cached multicore examples. Standalone Pico SDK
  and RP2350 hardware remain unvalidated.
- 100 iterations of four-SM allocation, fifth-SM rejection, hole reuse, index
  detach/reattach and full SM/program-memory release passed.
- 10,000 alternating position resets near +/-2^40 steps were read on the other
  core without inconsistent count/substep pairs. Logs also include reads while
  uninitialized, so their total read count is not the active-write count.
- 100 downstream motor-wrapper begin / 64-bit zero / end cycles passed.
- Stale detection, late acquisition, recovery, saturation, repeated end,
  index-zero reservation clearing and pull-up reconfiguration passed.
- The final stationary test log reports failures=0, errors=0 and both PIOs free;
  about 8.27 million reader iterations were recorded by its last telemetry line.

At 24.140–24.144V, downstream signed commands +3000 and -3000 each ran for 1.8s:

| Measurement | Positive | Negative |
|---|---:|---:|
| Position acquisitions | 1800 | 1800 |
| Speed updates, configured 10ms | 179 | 179 |
| Count changes between speed updates | 1619 | 1618 |
| Maximum measured wrapper update duration | 144µs | 130µs |
| Late acquisition flags | 0 | 0 |
| Logged speed range [step/s], sign-corrected by wrapper | 0 to +2540.64 | -2545.81 to 0 |
| Snapshot consistency errors | 0 | 0 |

Velocity status changed from warming up to valid. The stopped final wrapper
Snapshot had output=0 and Initialized=false after end. The earlier ~5973rpm
maximum-speed result belongs to the 0.2.0 bench; it was not repeated at maximum
speed after the 0.3.0 integration. Index registration was tested without a Z
signal; physical Z capture accuracy is not established by these tests.
