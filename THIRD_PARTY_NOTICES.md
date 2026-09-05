# Third-party notices

## Pico_PIO_Encoder implementation

The library implementation, examples and documentation are distributed under
[BSD-2-Clause](LICENSE), except for the PIO program and its generated header below.
Copyright (c) 2026 Ryota SUZUKI. Existing upstream notices are retained.

## PicoEncoder

- Upstream: https://github.com/pmarques-dev/PicoEncoder
- Authors credited by the original project: Paulo Marques, Pedro Pereira, Paulo Costa.
- The locally used upstream LICENSE states: Copyright (c) 2024, pmarques-dev.
- License: [BSD-2-Clause, upstream text](LICENSES/BSD-2-Clause-PicoEncoder.txt).
- Derived portions: quadrature sampling/PIO setup, transition-based velocity bounds
  and phase calibration. Pico_PIO_Encoder separates the estimator, changes position
  semantics and scheduling, adds cached publication, diagnostics and lifecycle handling.
- Relevant files: `src/Pico_PIO_Encoder.cpp`, `src/substep_encoder_motion.h`,
  `src/substep_encoder_estimator.h`, and related interface code.

## Raspberry Pi quadrature encoder substep PIO

- Upstream: https://github.com/raspberrypi/pico-examples/tree/master/pio/quadrature_encoder_substep
- File notice: Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
- License: [BSD-3-Clause, upstream project text](LICENSES/BSD-3-Clause-Raspberry-Pi.txt).
  The upstream project license carries its 2020 project copyright; the PIO file
  carries the more specific 2023 notice, which is retained in this distribution.
- Covered files: `src/substep_encoder.pio`, `src/substep_encoder.pio.h`, and the
  derived two-SM program `src/pair_encoder.pio` / `src/pair_encoder.pio.h`.
  The two-SM decoder reuses the counter/age protocol with independent input pins;
  its changes are Copyright (c) 2026 Ryota SUZUKI, under BSD-3-Clause.
- The PIO instruction program was retained through PicoEncoder. Program and symbol
  names were changed to `substep_encoder`; the checked-in header is generated code.

The root BSD-2-Clause license does not replace the BSD-3-Clause terms for these
PIO files. Retain these notices and the applicable license texts when redistributing
source or binaries, including firmware containing the PIO program.
