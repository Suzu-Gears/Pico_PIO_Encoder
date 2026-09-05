# On-device pull configuration tests

This SDK project checks `Pull::None`, `Pull::Up`, `Pull::Down`, the default,
reinitialization, invalid enum rejection and independent encoder/index settings.
It reads pad pull registers and verifies the idle input level for Up/Down.
It does not measure encoder frequency or generate quadrature waveforms.

Leave **GPIO2, GPIO3, GPIO6, GPIO7 and GPIO8 externally unconnected**.
Run this as standalone firmware with all PIO state machines available.
No additional test wires are needed. Do not use these pins if attached circuitry
drives them or depends on their bias.

With `PICO_SDK_PATH` and the toolchain configured:

```sh
cmake -S test/pico_sdk -B build-pulls -G Ninja -DPICO_BOARD=pico
cmake --build build-pulls
```

Use `pico2` for Pico 2 ARM or the appropriate SDK board name.
Flash `build-pulls/pull_modes.uf2` and open USB serial. Commands:

- `?`: identify the test firmware.
- `U`: run the tests; expect `phase_cases=40 index_cases=20`, `failures=0`, `pio_free=1`.
- `S`: acknowledge stop (and reassert zero/brake for the fixture option below).

On **RP2350 A2**, erratum E9 can cause the physical Down-level checks to fail
even when the pull registers are correct. An external resistor would change
what this test measures, so do not treat such a failure as a software regression
without checking the stepping and input voltage. A3/A4 fix E9; see the
[official datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
`Pull::None` only checks registers: floating input levels are unspecified.

## Local Nidec fixture

For the documented RP2040-Zero fixture, additionally configure:

```sh
-DPICO_BOARD=waveshare_rp2040_zero -DPICO_PIO_ENCODER_NIDEC_FIXTURE=ON
```

This option holds PWM GPIO15 HIGH (zero output) and BRAKE GPIO14 LOW (brake ON)
from startup. It performs no rotation test. Enable it only for that wiring.
GPIO27/28 and the GPIO28–10 jumper are left untouched. The option is OFF by
default; generic builds do not control motor pins.

Results and limits: [pull configuration validation](../../docs/PULL_CONFIGURATION_VALIDATION.md).
