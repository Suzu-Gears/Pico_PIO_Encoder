# 0.3.0 publication preparation — 2026-09-05

Historical measurements below were recorded under the development name SubstepEncoder.
Current public identifiers are Pico_PIO_Encoder; raw measurement logs are preserved.
For renamed-source build checks, see [release validation](RELEASE_VALIDATION.md).

The preparation changes documentation, metadata, notices, packaging and adds
`multicore_snapshot`. The encoder implementation is the previously validated
0.3.0 implementation; no motor operation or firmware upload was performed during
publication preparation.

| Check | Result |
|---|---|
| Arduino Lint 1.3.0, specification compliance, Library Manager submit rules | 0 errors, 1 warning |
| Warning LP042 | Repository URL returns 404 to an unauthenticated request while the repository is Private |
| Included sketches, Arduino Lint | No errors or warnings |
| Host estimator tests, GCC C++17, warnings as errors | All 16 test functions pass |
| New multicore_snapshot, RP2040-Zero / arduino-pico 6.0.0 | Compiles; 59,636 bytes flash, 9,292 bytes globals |
| Relative Markdown file links | Valid |
| Included PowerShell runner syntax | Valid |
| Distribution source tree | No executables, object files, UF2 or firmware backups |
| Repository status before publication | Private, no existing tags; not changed by this preparation |

The Lint URL warning must be rechecked after changing visibility. GitHub Actions
and public registry acceptance have not been run or asserted by these local checks.
RP2350 hardware and a standalone Pico SDK build remain unverified. Existing
hardware test evidence is documented separately in [VALIDATION_RESULTS](VALIDATION_RESULTS.md).

Both README languages, CHANGELOG, release text, maintainer information, upstream
license texts and publication instructions are included. See [RELEASING](RELEASING.md)
for the remaining publication operations.
