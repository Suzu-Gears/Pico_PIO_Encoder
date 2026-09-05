# Pico_PIO_Encoder 0.3.0 — Independent rates, snapshots and lifecycle

Historical measurements below were recorded under the development name SubstepEncoder.
Current public identifiers are Pico_PIO_Encoder; raw measurement logs are preserved.
For renamed-source build checks, see [release validation](RELEASE_VALIDATION.md).

Pico_PIO_Encoder is a PIO quadrature encoder library for RP2040/RP2350. Position
acquisition and velocity estimation have separate update intervals: for example,
read measured position at 1kHz while updating velocity at 100Hz.

This release provides:

- 64-bit measured counts and separate 1/64-step interpolated position.
- Transition-timed fractional velocity and elapsed-time stop detection.
- `latest()` for coherent cached Snapshot reads on another core.
- Initialization, velocity readiness, stale/late data and saturation diagnostics.
- `end()` and reinitialization with PIO/index resource release.
- Optional phase calibration and index/home capture; Z is not required.

Use the Earle F. Philhower arduino-pico core. Core 6.0.0 and RP2040-Zero are tested.
RP2350 and standalone Pico SDK support are present in the source, but have not
been hardware/build-validated respectively for this release. No additional
Arduino library is required. The optional motor bench uses the core's Wire library.

Validation includes 16 host tests, repeated resource allocation/reinitialization,
concurrent Snapshot reads and bidirectional 1kHz/100Hz motor runs. The earlier
24V bench reached about 5973rpm with no recorded missed 1kHz deadlines. These
are measurements on one fixture, not an independent accuracy or endurance rating.

Migration: `position()` is measured `int64_t` step and `speed()` is `float` step/s.
Interpolation is a separate value. Snapshot layout has changed. Only `latest()`
is intended for another core; acquisition/configuration/lifecycle have one owner.
Encoder objects cannot be copied or moved.

Start with `position_speed`, `multirate` or `multicore_snapshot`. Full API,
Japanese documentation, migration notes and test scope are included in the ZIP.

License: BSD-2-Clause for the library implementation; BSD-3-Clause for the
Raspberry Pi PIO program and generated header. Upstream notices and license
texts are included. Thanks to PicoEncoder's authors and Raspberry Pi.
