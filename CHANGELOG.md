# Changelog

## 0.4.0 — 2026-09-06

- Public name: **Pico_PIO_Encoder**. Include `Pico_PIO_Encoder.h`, instantiate
  `Pico_PIO_Encoder`, and link `pico_pio_encoder` in Pico SDK projects.
- Arduino RP2040/RP2350 and Pico SDK Pico/Pico 2 build matrices in CI;
  standalone SDK examples now show both wiring modes.
- English and Japanese release documentation, unit conversions and zeroing guide.
- `Pull::None`, `Pull::Up`, and `Pull::Down` for both wiring modes and the optional
  index input. The default remains `Pull::Up`; reinitialization replaces both
  pad pull bits. Invalid enum values are rejected before changing GPIO/resources.
  This replaces the unpublished boolean pull argument, with no compatibility overload.
- RP2350 A2 E9 electrical caveat in both READMEs, plus on-device pull tests and
  their RP2040/RP2350 SDK compile checks in CI.

The hardware records dated 2026-09-05 were produced under the development name
SubstepEncoder. Renaming does not change the decoder or estimator algorithms.
Earlier entries below record development iterations, not previous public releases.

- Explicit `beginConsecutive(A, B)` and `beginNonConsecutive(A, B)` APIs;
  no automatic mode fallback. Both validate wiring and report initialization errors.
- Consecutive mode uses 1 SM (4 encoders/PIO); nonconsecutive mode uses 2 SMs
  (2 encoders/PIO). Partial release/reuse preserves other encoders in the block.
- Consistent A/B-order direction, shared estimator, calibration, index, units,
  64-bit position, independently scheduled velocity and cross-core Snapshot.
- Snapshot reports mode, pins, SM consumption, read failures and retries.
  Bounded FIFO acquisition preserves old data on failure. `PioFault` requires restart.
- GPIO-window-aware allocation on RP2350B; incompatible pairs are rejected and
  a free block's previous window is restored after final release. Upper-pin hardware
  is not part of the RP2040 fixture validation.
- Removed the unpublished single-first-pin `begin` API. Examples now use the
  two explicitly named wiring methods; no compatibility overloads remain.
- English/Japanese wiring guides and a nonconsecutive example.
- Validation scope: [named modes](docs/PIN_MODES_VALIDATION.md).
- Standalone Pico SDK 2.1.1: RP2040-Zero and Pico 2 ARM example builds;
  RP2040 resource, lifecycle, synthetic, motor and multicore checks passed.
  See [SDK validation](docs/PICO_SDK_VALIDATION.md).

## 0.3.0 — 2026-09-05

### Added

- `latest()` for coherent cached Snapshot reads across RP2040/RP2350 cores.
- Initialization, velocity readiness, stale data, late acquisition and saturation flags.
- `positionValid()`, `speedValid()`, publication sequence and 64-bit acquisition time.
- `setMaxSampleAgeUs()` to configure acquisition/freshness diagnostics.
- `end()`, automatic resource cleanup on destruction, reusable SM slots and final PIO-block release.
- Saturating position arithmetic and recovery by resetting the position reference.
- Independent position/velocity operation in the Nidec integration, tested on RP2040-Zero.
- Japanese introduction, license notices, publication instructions and a cached multicore example.

### Changed

- Hardware and estimator access have a single-owner-context contract; other cores use `latest()`.
- Encoder objects cannot be copied or moved.
- Reinitialization clears measurement, index and calibration-learning state while preserving configuration.
- `begin(..., false)` clears pull-ups left by an earlier initialization.
- Velocity readiness is reset on restart/reversal and becomes valid after sufficient observations or a confirmed stop.

### Migration

Measured position is `int64_t` step, interpolation is separate `int64_t` substep,
and public speed is fractional `float` step/s. Snapshot layout is not a stable
serialization format. Review old structured bindings and integer narrowing.

The preceding development iteration (0.2.0) introduced measured 64-bit position,
independent velocity cadence, elapsed-time stop detection and count-based index
capture. These changes are included in 0.3.0; no earlier public tag is assumed.

### Validation

16 host test functions pass. The RP2040-Zero fixture passed lifecycle/resource
tests, concurrent Snapshot reads and short bidirectional tests at 1kHz position /
100Hz velocity. Earlier estimator tests covered low speed and a 24V maximum-speed
run. Scope and numerical results: [validation record](docs/VALIDATION_RESULTS.md).
