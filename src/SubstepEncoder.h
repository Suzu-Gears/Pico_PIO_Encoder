/*
  SubstepEncoder - high resolution quadrature encoder for RP2040/RP2350

  Reads a quadrature encoder with a PIO program that counts steps and
  timestamps transitions entirely in hardware (zero CPU load), and combines
  both into a high resolution position and speed estimate.

  Key properties:
   - correct at any read rate: getters refresh themselves, there is no
     update() cadence the user has to get right
   - 64-bit substep position that never wraps in practice
   - speed estimated from transition timing, accurate down to very low speeds
   - works both as an Arduino library (arduino-pico core) and as a plain
     Pico SDK library (no Arduino dependencies)

  Based on PicoEncoder by Paulo Marques, Pedro Pereira, Paulo Costa
  (https://github.com/pmarques-dev/PicoEncoder, BSD 2-clause).
  Distributed under the BSD 2-clause license, see LICENSE.
*/
#pragma once

#include <cstdint>

#include "hardware/pio.h"

#include "substep_encoder_estimator.h"

// internal GPIO IRQ dispatcher for index inputs, do not call directly
void substep_encoder_index_irq_dispatch();

class SubstepEncoder {
public:
  // each quadrature step is divided into 64 substeps; a 4x-counted encoder
  // with N pulses per revolution gives N * 4 * 64 substeps per revolution
  static constexpr int kSubstepsPerStep = substep_encoder::Estimator::kSubstepsPerStep;

  // one consistent reading (position and speed from the same refresh)
  struct Snapshot {
    int64_t position;       // quadrature steps
    float speed;            // steps per second
    uint32_t timestamp_us;  // when the underlying sample was taken
  };

  // Start tracking the encoder. The two encoder phases must be connected to
  // consecutive GPIOs and "firstPin" is the lower-numbered one. Pull-ups are
  // enabled by default (most encoders have open-collector outputs).
  //
  // The PIO program needs a full PIO block (all 32 instructions), so the
  // first encoder claims an entire free PIO; up to 4 encoders share one
  // block. Pass "pio" explicitly to control which block is used when other
  // PIO libraries (CAN, NeoPixel, ...) are in play.
  //
  // Returns 0 on success, -1 if no suitable PIO block is free.
  int begin(uint firstPin, bool pullUp = true);
  int begin(uint firstPin, PIO pio, bool pullUp = true);

  // ---- readings (self-refreshing, valid at any call rate) ----
  // Default units are quadrature steps (4x counting: PPR x 4 steps per
  // revolution, e.g. 400 for a 100 PPR encoder). The estimator still works
  // in substeps internally, so speed keeps its low-speed resolution as a
  // fractional value. Use the *Substeps() variants for the full 1/64-step
  // resolution.

  // position in steps (64-bit, does not wrap)
  int64_t position();

  // speed in steps per second (fractional)
  float speed();

  // full resolution position in substeps (64 per step)
  int64_t positionSubsteps();

  // full resolution speed in substeps per second
  int32_t speedSubsteps();

  // true if no transition has arrived for the idle timeout
  bool stopped();

  // position and speed from a single refresh, guaranteed consistent
  Snapshot read();

  // ---- control ----

  // force a re-read of the hardware right now (getters do this on their own
  // when the last reading is older than the min refresh interval)
  void refresh();

  // set the current position in steps (default: zero it)
  void resetPosition(int64_t to = 0);

  // getters trigger a hardware re-read when the last reading is older than
  // this. Default 100us: consecutive reads in one control cycle share one
  // sample, while any sane loop rate always gets fresh data
  void setMinRefreshIntervalUs(uint32_t us) {
    min_refresh_interval_us_ = us;
  }

  // if no transition arrives for this long, speed snaps to zero (the
  // estimate already decays towards zero before that). Default 50ms; raise
  // it to track extremely slow movement, lower it to detect stops faster
  void setIdleTimeoutUs(uint32_t us) {
    estimator_.setIdleTimeoutUs(us);
  }

  // ---- index input: Z phase, limit switch, home sensor (optional) ----
  // Latches the encoder position at the moment of an edge on an ordinary
  // GPIO. The interrupt handler only records a timestamp; the position at
  // that instant is reconstructed on the next read from the timestamp and
  // the speed estimate (typically accurate to a few substeps).
  //
  // Use cases:
  //  - encoder Z/index phase: absolute position within one revolution
  //  - limit switch on a linear axis: homing, re-checked on every hit
  //  - repeatability/drift checks: compare lastIndexPosition() between hits
  //
  // Call attachIndex() from the core that should service the interrupt
  // (typically core 0); attach all index pins from the same core.
  // For a mechanical switch pass a debounce time (e.g. 10000us); encoder
  // Z outputs are clean and can use the default of 0.
  //
  // Returns 0 on success, -1 if already attached or no slot is free.

  int attachIndex(uint pin, bool onRisingEdge = true, bool pullUp = true,
                  uint32_t debounceUs = 0);
  void detachIndex();

  // true once at least one index event has been latched
  bool indexSeen();

  // number of index events so far (counted in the interrupt, always fresh)
  uint32_t indexCount() const {
    return index_isr_count_;
  }

  // latched position of the most recent index event, in steps
  int64_t lastIndexPosition();

  // one-shot homing: when the next index event arrives, shift the position
  // reference so that the latched point equals positionAtIndex (in steps)
  void zeroOnNextIndex(int64_t positionAtIndex = 0);

  // true until the event armed by zeroOnNextIndex() has been processed
  bool zeroPending();

  // distance in steps between the two most recent index events (0 until
  // two events have been latched). For a Z phase this should equal +/- one
  // revolution in steps: any deviation means lost or extra steps
  int64_t lastIndexSpacing();

  // ---- unit conversion helpers (optional) ----
  // Set how many quadrature steps one revolution has (PPR x 4; e.g. a
  // 100 PPR encoder counted 4x -> 400). The helpers below return 0 until
  // this is set. The core API stays in integer substeps.
  // revolutions()/angleRad() cover the full multi-turn range in double
  // (numerically exact for ~a century of travel, but double is soft-float
  // on RP2040/RP2350 — avoid in tight loops). For a fast, forever-lossless
  // angle use the wrapped-angle helpers further below

  void setStepsPerRev(uint32_t steps) {
    substeps_per_rev_ = (int64_t)steps * kSubstepsPerStep;
    rad_per_substep_ = (float)(6.283185307179586 / (double)substeps_per_rev_);
    deg_per_substep_ = (float)(360.0 / (double)substeps_per_rev_);
  }

  double revolutions() {
    return substeps_per_rev_ ? (double)positionSubsteps() / (double)substeps_per_rev_ : 0.0;
  }

  double angleRad() {
    return revolutions() * 6.283185307179586;
  }

  float revPerSec() {
    return substeps_per_rev_ ? (float)speedSubsteps() / (float)substeps_per_rev_ : 0.0f;
  }

  float rpm() {
    return revPerSec() * 60.0f;
  }

  float radPerSec() {
    return revPerSec() * 6.2831853f;
  }

  // ---- absolute angle within one revolution (lossless) ----
  // The int64 substep position is the ground truth. Converting it straight
  // to a float angle degrades once the travel exceeds ~16.7M substeps
  // (float has 24 mantissa bits), and double math is soft-float on these
  // chips. The wrapped angle avoids both: the modulo is taken in INTEGER
  // substeps first and only the small remainder (< substeps per rev, fits
  // a float exactly to ~1/600 substep) is converted. Full substep
  // resolution at any mileage, cheap single-precision math.
  // Combined with zeroOnNextIndex() on a Z phase this is an absolute
  // within-revolution angle. Requires setStepsPerRev().

  // substeps into the current revolution, [0, stepsPerRev * 64)
  int32_t positionInRevSubsteps() {
    if (substeps_per_rev_ == 0) return 0;
    int64_t m = positionSubsteps() % substeps_per_rev_;
    if (m < 0) m += substeps_per_rev_;
    return (int32_t)m;
  }

  // angle within the current revolution, [0, 2*pi) / [0, 360)
  float angleInRevRad() {
    return (float)positionInRevSubsteps() * rad_per_substep_;
  }

  float angleInRevDeg() {
    return (float)positionInRevSubsteps() * deg_per_substep_;
  }

  // completed revolutions (floor), pairs with the wrapped angle above to
  // represent an exact multi-turn absolute position
  int64_t turns() {
    if (substeps_per_rev_ == 0) return 0;
    const int64_t p = positionSubsteps();
    int64_t t = p / substeps_per_rev_;
    if (p < 0 && (p % substeps_per_rev_) != 0) t--;
    return t;
  }

  // ---- phase size calibration (optional) ----
  // real encoders have slightly unequal phase sizes, which adds ripple to
  // the speed estimate. When enabled, calibration runs piggybacked on the
  // normal refreshes; spin the encoder steadily until calibrationReady().
  // Save getPhases() and restore it with setPhases() to skip the procedure

  void enableAutoCalibration(bool on = true) {
    if (on && !auto_calibrate_) estimator_.resetCalibration();
    auto_calibrate_ = on;
  }

  bool calibrationReady() const {
    return estimator_.calibrationReady();
  }

  int getPhases() const {
    return estimator_.getPhases();
  }

  void setPhases(int phases) {
    estimator_.setPhases(phases);
  }

  // direct access to the underlying estimator (advanced use)
  substep_encoder::Estimator &estimator() {
    return estimator_;
  }

private:
  substep_encoder::Estimator estimator_;
  PIO pio_ = nullptr;
  uint sm_ = 0;
  uint32_t clocks_per_us_ = 1;
  uint32_t last_refresh_us_ = 0;
  uint32_t min_refresh_interval_us_ = 100;
  bool auto_calibrate_ = false;
  int64_t substeps_per_rev_ = 0;
  float rad_per_substep_ = 0.0f;
  float deg_per_substep_ = 0.0f;

  // index input state (written by the interrupt handler)
  bool index_attached_ = false;
  uint index_pin_ = 0;
  uint32_t index_debounce_us_ = 0;
  volatile uint32_t index_isr_count_ = 0;
  volatile uint32_t index_isr_us_ = 0;
  uint32_t index_processed_count_ = 0;
  int64_t last_index_position_ = 0;
  int64_t prev_index_position_ = 0;
  uint32_t index_latch_count_ = 0;
  bool index_latched_ = false;
  bool zero_armed_ = false;
  int64_t zero_target_ = 0;

  void readSample(substep_encoder::Sample *s);
  void maybeRefresh();
  void processIndexEvents();
  void onIndexIrq();

  friend void substep_encoder_index_irq_dispatch();
};
