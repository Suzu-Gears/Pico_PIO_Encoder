# Explicit wiring modes (0.4.0)

Historical measurements below were recorded under the development name SubstepEncoder.
Current public identifiers are Pico_PIO_Encoder; raw measurement logs are preserved.
For renamed-source build checks, see [release validation](RELEASE_VALIDATION.md).

The public API consists of `beginConsecutive(A, B)` and
`beginNonConsecutive(A, B)`, with optional PIO and `Pull` arguments (default `Pull::Up`).
The later pull-selection addition is covered in [pull configuration validation](PULL_CONFIGURATION_VALIDATION.md).
There is no automatic fallback and no legacy `begin()` overload.
All subsequent measurement, calibration, index, unit, Snapshot and lifecycle
methods are shared. A/B order defines direction in both modes.

## Acceptance checks

- Host tests exercise the existing estimator and pin/window validation, including
  reversed adjacent pins, separated pins, duplicates, out-of-range pins and
  RP2350B GPIO-window boundaries.
- RP2040-Zero exercises four consecutive instances, two nonconsecutive instances,
  rejection of mixing programs within a block, partial release/reuse, mode changes
  after final release and simultaneous allocation of four plus two across blocks.
- Both modes exercise 64-bit reset, index capture/homing, cached-data freshness,
  forced PIO timeout, held data and timestamp on failure, and end/reinitialization.
- Ordered and reversed A/B arguments are checked through forward/reverse synthetic
  motion in all four initial phases, using the actual public API.
- Independent two-pair synthetic inputs and the Nidec motor fixture check the
  nonconsecutive mode while another core reads only published Snapshots.

RP2040 fixture: encoder signals GPIO27/28, additional GPIO28--GPIO10 jumper,
DIR26, active-low PWM15, active-low brake14, INA219 on SDA4/SCL5.
GPIO10 remains an input. Synthetic tests drive only unconnected GPIO2/3/6/7.
Motor commands are bounded in duration, use INA219 feedback, and stop with brake on.

## Timing tradeoffs

| Mode | SMs/encoder | Encoders/PIO | Input-check loop |
|---|---:|---:|---:|
| Consecutive | 1 | 4 | 13 clocks |
| Nonconsecutive | 2 | 2 | 17 clocks |

Both use 32 shared instruction words. Nonconsecutive acquisition brackets one
phase's read with two reads of the other phase, retrying at most eight times.
If a coherent sample cannot be obtained, old data remains with `ReadFailed`.
A FIFO-word timeout latches `PioFault` until end/reinitialization.
Counters `read_failures` and `read_retries` reset on reinitialization/end.

This protocol is not a simultaneous hardware latch. Extremely short net-zero
excursions cannot always be distinguished. Invalid two-bit transitions need not
produce the same behavior as the consecutive decoder's state table.
The independent IN and JMP pin samples are three clocks apart. The cycle-level
emulator sweep passed all tested phases/directions/alignments for state durations
20–24 clocks; failures occur at shorter durations. This is not an electrical
maximum-frequency guarantee or a guarantee that software can acquire at that rate.

RP2350 and RP2350B builds cover the extra PIO and GPIO-window code paths.
Upper GPIOs and RP2350 hardware are not tested on the RP2040 fixture.
The Arduino results below are supplemented by standalone Pico SDK 2.1.1 builds
and RP2040 hardware checks in [SDK validation](PICO_SDK_VALIDATION.md).

## Results — 2026-09-05

The final tests used the public `Pico_PIO_Encoder` class and both named initializers,
not the earlier experimental decoder class.

| Check | Result |
|---|---|
| Host estimator and pin/window tests | All 17 test functions passed |
| RP2040-Zero Arduino examples | All 8 examples compiled with arduino-pico 6.0.0 |
| Pico 2 / RP2350B | Nonconsecutive example and a 48-GPIO window exercise compiled |
| Resource and lifecycle exercise | 200 cycles, 0 failures; both PIO blocks fully released |
| Direction and initial phase | 8 cases, 0 failures |
| Two independent synthetic encoders | 12 cases, 0 failures |
| Arduino lint, specification/submit | 0 errors; 1 warning: repository URL returned HTTP 404 |

Motor tests compared one consecutive instance on GPIO27/28 with two nonconsecutive
instances on GPIO27/10, on separate PIO blocks. Supply measured 11.800 V through
INA219. Each run requested 250 mA for 2 seconds with a bounded PWM output; this
was not a maximum-speed or 24 V qualification. Acquisition ran at 1 kHz.

| Measurement | Forward, 100 Hz speed update | Reverse, 1 kHz speed update |
|---|---:|---:|
| Acquisition cycles | 2,000 | 2,000 |
| Settled count, all three instances | +17,219 | −17,255 |
| Settled count difference | 0 | 0 |
| Largest count difference during sequential acquisition | 2 steps | 2 steps |
| Maximum combined acquisition time, all three instances | 208 µs | 243 µs |
| Maximum acquisition timestamp separation | 184 µs | 215 µs |
| Missed 1 kHz ticks | 0 | 0 |
| Failed acquisitions, either nonconsecutive instance | 0 | 0 |
| Bracket retries, first / second instance | 36 / 17 | 48 / 20 |
| GPIO10 / GPIO28 disagreement, 2,000 simultaneous GPIO samples | 0 | 0 |

The other core made 5,826,048 cached Snapshot reads by the end of the second
run, with zero consistency errors. The combined acquisition times exclude the
rest of the control loop, including current sensing and logging. Moving-count
comparisons are sequential and include acquisition timestamp separation; settled
counts provide the direct count comparison. These results do not measure absolute
speed accuracy against an independent speed reference.

An earlier GPIO27/10 run failed while GPIO27/28 worked. Reseating the GPIO28--GPIO10
jumper resolved the discrepancy; the table reports the subsequent complete runs.
Both runs ended with output zero and brake on.
