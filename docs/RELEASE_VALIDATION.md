# Release preparation — 2026-09-06

Public library name: **Pico_PIO_Encoder**, version **0.4.0**.
The header and class are `Pico_PIO_Encoder`; the CMake target is `pico_pio_encoder`.

## Name and documentation update (before pull-selection addition)

- All 12 library source files were compared against the preceding implementation.
  At this stage, the only source changes were the public class/header name. Decoder, estimator,
  pin allocation and Snapshot synchronization behavior are unchanged.
- Both README languages describe the current wiring modes, Arduino and SDK setup,
  angle/speed unit helpers, zeroing and tested hardware limits.
- The standalone SDK project includes consecutive and nonconsecutive examples.
- The Arduino basic example prints 64-bit position and turn values without
  narrowing them to 32-bit `long`.
- CI includes host tests, Arduino Lint, Arduino RP2040/RP2350 builds and SDK
  Pico/Pico 2 ARM builds, with arduino-pico 6.0.0 and SDK 2.1.1.

The subsequent selectable-pull API changes GPIO initialization and index setup.
Its separate checks are recorded in [pull configuration validation](PULL_CONFIGURATION_VALIDATION.md).
The results below describe the preceding rename stage, not a repeat of all checks
after the pull API addition.

## Local checks at the rename stage

| Check | Result |
|---|---|
| C++17 host tests, GCC with warnings treated as errors | Passed |
| Arduino examples, arduino-pico 6.0.0 | All 8 examples passed on RP2040-Zero and Pico 2 ARM: 16 builds |
| Pico SDK 2.1.1, RP2040-Zero | Both examples built, ELF/UF2 generated |
| Pico SDK 2.1.1, Pico 2 ARM | Both examples built, ELF/UF2 generated |
| SDK source dependency check | No Arduino sources or include paths |
| Nidec integration, RP2040-Zero | Installed `multirate_snapshot` compiled after updating the library reference |
| actionlint 1.7.12 | Passed; external ShellCheck not run |
| Arduino Lint, specification / submit | 0 errors; 1 warning: repository URL returned HTTP 404 |
| README / documentation consistency | Local links, README anchors and fenced blocks checked |

The public description uses **RP2040 / RP235X**, with RP235X covering
RP2350A/B and RP2354A/B. Compiler targets and measured hardware are identified
by their actual RP2350 names. This wording does not expand the hardware evidence.

The new GitHub URL must resolve publicly before publication checks are complete.
The GitHub-hosted workflow has not been run by this local preparation. Passing
actionlint and local builds does not claim a successful hosted CI run.

## Hardware evidence

Hardware tests were performed on RP2040-Zero under the development name
SubstepEncoder on 2026-09-05. This rename did not flash the board or run the motor.
See [wiring-mode validation](PIN_MODES_VALIDATION.md) and
[standalone SDK validation](PICO_SDK_VALIDATION.md). The source comparison above
connects those algorithm tests with this renamed release.

RP2350 hardware, including upper GPIO windows, remains unverified. Builds do not
establish electrical limits or maximum count rate. No GitHub push, release, tag,
repository visibility change or Library Manager registration was performed here.
