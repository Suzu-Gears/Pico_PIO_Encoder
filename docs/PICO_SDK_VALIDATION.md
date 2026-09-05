# Standalone Pico SDK validation — 2026-09-05

Historical measurements below were recorded under the development name SubstepEncoder.
Current public identifiers are Pico_PIO_Encoder; raw measurement logs are preserved.
For renamed-source build checks, see [release validation](RELEASE_VALIDATION.md).

Pico_PIO_Encoder 0.4.0 builds and runs with standalone Pico SDK 2.1.1 using
the same library sources as Arduino. No library or CMake changes were required.

## Environment and builds

| Component | Tested version / target |
|---|---|
| Host | Windows, PowerShell |
| Pico SDK | 2.1.1 |
| Arm GNU toolchain | 14.2.Rel1, GCC 14.2.1 |
| CMake / Ninja | 3.31.5 / 1.12.1 |
| Python / picotool | 3.12.0 / 2.1.1 |
| Public SDK example | RP2040-Zero (`waveshare_rp2040_zero`) and Pico 2 (`pico2`, ARM) |
| Hardware | RP2040-Zero, system clock 125 MHz |

Both public example builds produced ELF and UF2 files. The SDK validation firmware
also built and ran on RP2040-Zero. Each compilation database included the public
`Pico_PIO_Encoder.cpp` and no Arduino sources or include paths.

With the SDK and toolchain configured, from the library root:

```sh
cmake -S pico_sdk_example -B build-sdk -DPICO_BOARD=waveshare_rp2040_zero
cmake --build build-sdk
cmake -S pico_sdk_example -B build-sdk-pico2 -DPICO_BOARD=pico2
cmake --build build-sdk-pico2
```

The runtime bench uses SDK GPIO, PWM, I2C, multicore and watchdog APIs directly.
Arduino CLI was used only to transfer its SDK-built UF2 through the board's
BOOTSEL drive. It did not compile or provide an Arduino runtime for this test.

## RP2040 hardware checks

| Check | Result |
|---|---|
| Resource allocation, release and reinitialization | 200 cycles, 0 failures; both PIO blocks released |
| Direction / initial phase, both modes | 8 cases, 0 failures |
| Two independent nonconsecutive encoders | 12 synthetic cases, 0 failures |
| Additional lifecycle checks | 64-bit reset, index capture/homing, stale/late data, forced PIO timeout and retained timestamps passed |

The fixture uses encoder GPIO27/28 with GPIO28 connected to GPIO10. One
consecutive instance reads 27/28 on PIO0; two nonconsecutive instances read 27/10
on PIO1. GPIO10 remains an input. Motor DIR is GPIO26, active-low PWM GPIO15,
active-low brake GPIO14; INA219 uses SDA4/SCL5. Synthetic tests use otherwise
unconnected GPIO2/3/6/7, with GPIO8 for index checks.

Each motor run requested +250 or −250 mA for two seconds, with a PWM limit of
16000/32767 (about 49%). These are current targets, not measured steady currents.
INA219 measured 11.796 V during both runs and 11.800 V after stopping.
The bench checks supply/current bounds and stops on USB disconnect or watchdog
expiry. Every completed run sets zero output and enables the brake.

| Measurement | Forward, 10 ms velocity interval | Reverse, 1 ms velocity interval |
|---|---:|---:|
| Acquisition period | 1 ms | 1 ms |
| Acquisition samples | 2,001 | 2,001 |
| Settled counts, all three instances | −17,217 | +17,240 |
| Settled count difference | 0 | 0 |
| Largest difference during sequential acquisition | 2 steps | 2 steps |
| Maximum combined acquisition time | 208 µs | 333 µs |
| Maximum first-to-last acquisition timestamp gap | 177 µs | 275 µs |
| Missed acquisition ticks | 0 | 0 |
| Failed acquisitions, all instances | 0 | 0 |
| Velocity publications, reference / second nonconsecutive | 200 / 200 | 2,000 / 1,999 |
| Position changes between reference velocity publications | 1,787 | 0 |
| Cached Snapshot reads during the run | 622,592 | 570,880 |
| Snapshot consistency errors | 0 | 0 |

The raw A/B order makes the forward motor command negative in this fixture.
All three encoders use the same sign; this bench does not apply the Nidec
integration's sign correction. Sequential reads occur at different times, so
their moving counts need not match exactly. The comparison after settling does.
Velocity intervals are serviced by acquisitions; timestamp jitter can change
the number of publications slightly, rather than creating a separate timer.

Another core read only cached Snapshots. The run counters above total 1,193,472
reads and include setup and settling within each command, not just powered motion.
Checks covered metadata and consistency between measured and interpolated position.

The first motor comparison was made before the GPIO28–GPIO10 jumper was connected:
the reference moved while both GPIO10-based instances remained at zero. The table
reports the successful repeat after connecting the jumper, not that failed setup.

## Scope

- This confirms standalone SDK compilation and RP2040 execution of both wiring
  modes, independent velocity scheduling, lifecycle and cached multicore access.
- RP2350 ARM was compile-tested only. RP2350 hardware, upper GPIOs, RISC-V and
  other SDK/toolchain versions were not qualified by this run.
- Motor testing was bounded at approximately 12 V, not a maximum-speed or 24 V
  qualification. Count agreement does not establish absolute mechanical accuracy
  or prove the absence of edges missed by all decoders.
- These are local build and hardware checks, not an SDK CI job or exhaustive
  concurrency proof. See [mode timing limits](PIN_MODES_VALIDATION.md).
