# Pull configuration validation — 2026-09-06

Public API: `Pico_PIO_Encoder::Pull::{None, Up, Down}`. Both named wiring
initializers, with automatic or explicit PIO selection, default to `Pull::Up`.
The optional index input has an independent pull argument with the same default.

The change replaces the unpublished boolean argument. `gpio_set_pulls()` sets
both pad bits on each initialization; `None` clears both. Invalid enum values
return -2 before allocation or GPIO changes. Decoder, estimator, PIO programs,
Snapshot synchronization and resource allocation algorithms are unchanged.

## On-device checks

Firmware: [test/pico_sdk](../test/pico_sdk/README.md), SDK 2.1.1,
`PICO_BOARD=waveshare_rp2040_zero`, `PICO_PIO_ENCODER_NIDEC_FIXTURE=ON`.

On RP2040-Zero, two successive USB `U` commands produced:

```text
RESULT pulls phase_cases=40 index_cases=20 checks=660 failures=0 pio_free=1
STOP PWM15=HIGH BRAKE14=LOW
RESULT pulls phase_cases=40 index_cases=20 checks=660 failures=0 pio_free=1
STOP PWM15=HIGH BRAKE14=LOW
```

- Both wiring modes and both PIO-selection overloads: all nine previous/new
  pull combinations, plus omitted-argument default after Down (40 cases).
- Index input: all nine previous/new combinations plus the default after Down
  in each mode (20 cases); index settings leave A/B settings unchanged.
- Pad pull registers and biased input levels checked; floating levels are not asserted.
- Invalid values leave GPIO/resources unchanged. Duplicate initialization and
  duplicate index attachment preserve the existing pull setting.
- Two encoders in the same PIO use different pulls independently.
- Position acquisition succeeds with each pull; all SMs are released afterward.

GPIO2/3/6/7/8 were unconnected test inputs. The motor stayed at zero output with
brake ON. This was not a rotation or maximum-frequency test.

## Build checks for this change

| Check | Result |
|---|---|
| SDK 2.1.1 pull-test firmware | RP2040-Zero and Pico 2 ARM built |
| SDK 2.1.1 examples, both wiring modes | Both targets built for each board |
| Arduino-pico 6.0.0 | `position_speed`, `nonconsecutive`, `index_homing` built for RP2040-Zero and Pico 2 ARM (6 builds) |
| Nidec integration, RP2040-Zero | Installed `multirate_snapshot` built against the updated public sources |
| actionlint 1.7.12 | Passed; external ShellCheck not run |
| Arduino Lint, specification / submit | 0 errors; 1 warning: unpublished repository URL returned HTTP 404 |
| Documentation | Local links, README anchors and fenced blocks checked |

The earlier [release preparation record](RELEASE_VALIDATION.md) covers the full
eight-example Arduino matrix before this API change. CI now also compiles the
on-device pull tests for Pico/Pico 2; it does not run them on hardware.

## RP2350 scope

RP2350/RP2354 hardware remains unverified. On **RP2350 A2**, GPIO erratum E9
can cause physical Down-level failures even when registers are set correctly.
A3/A4 fix E9. Verify the chip stepping before interpreting pull-test results;
see the [README electrical guidance](../README.md) and
[official datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

The library does not substitute pull modes automatically or claim that software
configuration fixes E9. The API remains available for suitable external circuits
and corrected silicon.
