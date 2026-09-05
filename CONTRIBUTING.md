# Contributing

Use [GitHub Issues](https://github.com/Suzu-Gears/Pico_PIO_Encoder/issues) for bugs
and proposals, and pull requests for changes. This project is maintained by
Ryota SUZUKI (`suzuki.ryota.ua@tut.jp`). No response-time commitment is made.

For a reproducible report include:

- Library version, board, Arduino core or Pico SDK version, and compiler output.
- A/B/index wiring, pull-ups, logic levels and encoder steps per revolution.
- Acquisition interval, speed interval, idle timeout and freshness limit.
- Which core/context calls each API, including index IRQ and object destruction.
- Snapshot fields/flags and a minimal sketch; describe expected and observed behavior.

Do not include credentials or unrelated personal files in logs. Motor-test reports
should state the supply voltage, drive command, duration and actual motion.

Run `make -C test/host run` with a C++17 compiler for estimator changes. Compile
the affected Arduino examples with arduino-pico. Hardware tests must be identified
as such: passing builds do not imply hardware validation. Preserve the PIO timing
contract and upstream notices when modifying derived code.

The CI workflow uses arduino-pico 6.0.0 on RP2040/RP2350 and Pico SDK 2.1.1 on
Pico/Pico 2 ARM. Build `pico_sdk_example` for SDK changes; it includes consecutive
and nonconsecutive examples. Keep both README languages and the Arduino/CMake
installation instructions consistent. Release steps are in [RELEASING](docs/RELEASING.md).

Keep hardware access in one owner context; changes to Snapshot publication must
preserve the absence of C++ data races. Document API/behavior changes in CHANGELOG.
