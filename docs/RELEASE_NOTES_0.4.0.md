# Pico_PIO_Encoder 0.4.0 — Explicit wiring modes

Arduino and standalone Pico SDK share one library. Use `Pico_PIO_Encoder.h`,
class `Pico_PIO_Encoder`, and CMake target `pico_pio_encoder`.

Choose the initializer that matches the wiring:

```cpp
encoder.beginConsecutive(2, 3);      // adjacent A/B: up to 4 encoders per PIO
encoder.beginNonConsecutive(2, 10); // separated A/B: up to 2 encoders per PIO
```

These are alternatives; call one and check its return value before measuring.
An incompatible consecutive pair is rejected instead of silently switching mode.

Both modes share measured 64-bit position, interpolation, independently scheduled
velocity, cached multicore Snapshot, calibration, index/homing, unit helpers and
resource release/reinitialization. A/B order sets direction consistently.

Both initializers and `attachIndex()` accept `Pico_PIO_Encoder::Pull::None`,
`Pull::Up`, or `Pull::Down` (using `Pull = Pico_PIO_Encoder::Pull`). The default
is internal pull-up. Reinitialization replaces the previous pull setting.
RP2350 A2 users must account for GPIO erratum E9 when relying on a low bias;
see the wiring guides and [pull configuration validation](PULL_CONFIGURATION_VALIDATION.md).

Snapshot includes the selected mode, pins, SM consumption and acquisition diagnostics.
Failed acquisitions retain the last data and timestamp; a FIFO timeout requires
end/reinitialization. Each mode occupies a full 32-instruction block, so different
modes use different PIO blocks. RP2350B allocation respects the shared GPIO window.

The nonconsecutive decoder uses more SMs, a 17-clock rather than 13-clock input loop,
and more acquisition work. It is not a simultaneous hardware latch and does not
have identical invalid-transition behavior or timing limits.

See the [English wiring guide](../README.md), [日本語の配線ガイド](../README.ja.md)
and [validation results and scope](PIN_MODES_VALIDATION.md). RP2350 builds are checked;
RP2350 hardware remains unverified. Standalone Pico SDK 2.1.1 builds and
RP2040-Zero hardware checks pass; see [SDK validation](PICO_SDK_VALIDATION.md).

GitHub Actions includes host tests, Arduino Lint, all Arduino examples on
RP2040/RP2350, and both SDK examples on Pico/Pico 2 ARM. See the
[release validation record](RELEASE_VALIDATION.md) for local checks and CI status.
