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

class SubstepEncoder {
public:
  // each quadrature step is divided into 64 substeps; a 4x-counted encoder
  // with N pulses per revolution gives N * 4 * 64 substeps per revolution
  static constexpr int kSubstepsPerStep = substep_encoder::Estimator::kSubstepsPerStep;

  // one consistent reading (position and speed from the same refresh)
  struct Snapshot {
    int64_t position;      // substeps
    int32_t speed;         // substeps per second
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

  // position in substeps (64-bit, does not wrap)
  int64_t position();

  // speed in substeps per second
  int32_t speed();

  // true if no transition has arrived for the idle timeout
  bool stopped();

  // position and speed from a single refresh, guaranteed consistent
  Snapshot read();

  // ---- control ----

  // force a re-read of the hardware right now (getters do this on their own
  // when the last reading is older than the min refresh interval)
  void refresh();

  // set the current position (default: zero it)
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

  void readSample(substep_encoder::Sample *s);
  void maybeRefresh();
};
