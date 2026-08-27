/*
  SubstepEncoder - high resolution quadrature encoder for RP2040/RP2350

  This file contains the pure estimation logic: it converts raw PIO samples
  (step count + transition timing) into a 64-bit substep position and a
  substep-per-second speed estimate. It has no hardware dependencies, so it
  can be unit-tested on a host machine.

  The estimation algorithm is derived from PicoEncoder by Paulo Marques,
  Pedro Pereira, Paulo Costa (BSD 2-clause), with the following changes:
   - stop detection is wall-clock based instead of sample-count based, so
     results are independent of how often the estimator is updated
   - position is accumulated into 64 bits and never wraps in practice
   - division-by-zero guards for very high update rates
   - phase calibration is fed with the same samples as the estimator instead
     of reading the hardware separately

  Distributed under the BSD 2-clause license.
*/
#pragma once

#include <cstdint>

namespace substep_encoder {

// One raw reading derived from the PIO state machine.
struct Sample {
  uint32_t step;           // cumulative quadrature step count (wraps at 2^32)
  uint32_t sample_us;      // timestamp when the sample was taken
  uint32_t transition_us;  // timestamp of the most recent step transition
  bool forward;            // direction of the most recent transition
};

class Estimator {
public:
  // each quadrature step is divided into 64 substeps
  static constexpr int kSubstepsPerStep = 64;

  // 4 equal phases, used before any calibration is applied
  static constexpr int kDefaultPhases = 0x404040;

  // if no transition arrives for this long, the encoder is considered
  // stopped and speed snaps to exactly zero. Note that the estimate already
  // decays towards zero before that (the step boundaries bound the possible
  // speed), so this can be much larger than the sampling period.
  static constexpr uint32_t kDefaultIdleTimeoutUs = 50000;

  Estimator() {
    setPhases(kDefaultPhases);
  }

  // ---- configuration ----

  void setIdleTimeoutUs(uint32_t us) {
    idle_timeout_us_ = us;
  }

  uint32_t idleTimeoutUs() const {
    return idle_timeout_us_;
  }

  // set the relative phase sizes (result of a previous calibration)
  void setPhases(int phases) {
    calibration_data_[0] = 0;
    calibration_data_[1] = (phases & 0xFF);
    calibration_data_[2] = calibration_data_[1] + ((phases >> 8) & 0xFF);
    calibration_data_[3] = calibration_data_[2] + ((phases >> 16) & 0xFF);
  }

  int getPhases() const {
    return calibration_data_[1] |
           ((calibration_data_[2] - calibration_data_[1]) << 8) |
           ((calibration_data_[3] - calibration_data_[2]) << 16);
  }

  // ---- lifecycle ----

  // (re)initialize the internal state from the first sample
  void reset(const Sample &s) {
    stopped_ = true;
    speed_ = 0;
    speed_2_20_ = 0;
    step_ = s.step;
    prev_sample_us_ = s.sample_us;
    prev_trans_us_ = s.transition_us;
    prev_trans_pos_ = 0;
    prev_low_ = 0;
    prev_high_ = 0;
    internal_position_ = stepStartPos(s.step) + kSubstepsPerStep / 2;
    prev_internal_position_ = internal_position_;
    position_ = 0;
  }

  // update the speed and position estimates from a new sample. Correct at
  // any sampling rate: all timing logic uses wall-clock time, never sample
  // counts
  void update(const Sample &s) {
    // the current step gives lower and upper substep bounds for the position
    const uint32_t low = stepStartPos(s.step);
    const uint32_t high = stepStartPos(s.step + 1);

    // wall-clock based stop detection
    if (!stopped_ && (uint32_t)(s.sample_us - prev_trans_us_) > idle_timeout_us_) {
      speed_ = 0;
      speed_2_20_ = 0;
      stopped_ = true;
    }

    if (s.step != step_) {
      // there was at least one transition since the last sample. The
      // transition position depends on the direction of the move
      const uint32_t transition_pos = s.forward ? low : high;

      // if we are not stopped, the previous transition is valid and we can
      // use the transition-to-transition timing to estimate speed
      if (!stopped_) {
        speed_2_20_ = calcSpeed((int32_t)(transition_pos - prev_trans_pos_),
                                (int32_t)(s.transition_us - prev_trans_us_));
      }

      stopped_ = false;
      prev_trans_pos_ = transition_pos;
      prev_trans_us_ = s.transition_us;
    }

    if (!stopped_) {
      // the step boundaries bound the possible speed: this gives a non-zero
      // estimate right after starting to move and makes the estimate decay
      // while decelerating, even before any new transition arrives
      int32_t speed_high, speed_low;
      if ((int32_t)(prev_trans_us_ - prev_sample_us_) > 0 &&
          (int32_t)(prev_trans_us_ - prev_sample_us_) > (int32_t)(s.sample_us - prev_trans_us_)) {
        speed_high = calcSpeed((int32_t)(prev_trans_pos_ - prev_low_),
                               (int32_t)(prev_trans_us_ - prev_sample_us_));
        speed_low = calcSpeed((int32_t)(prev_trans_pos_ - prev_high_),
                              (int32_t)(prev_trans_us_ - prev_sample_us_));
      } else {
        speed_high = calcSpeed((int32_t)(high - prev_trans_pos_),
                               (int32_t)(s.sample_us - prev_trans_us_));
        speed_low = calcSpeed((int32_t)(low - prev_trans_pos_),
                              (int32_t)(s.sample_us - prev_trans_us_));
      }
      if (speed_2_20_ > speed_high) speed_2_20_ = speed_high;
      if (speed_2_20_ < speed_low) speed_2_20_ = speed_low;

      // convert "substeps per 2^20 us" to "substeps per second"
      speed_ = (int32_t)(((int64_t)speed_2_20_ * 62500LL) >> 16);

      // estimate the position by extrapolating from the last transition,
      // bounded by the current step boundaries
      uint32_t est = prev_trans_pos_ +
                     (uint32_t)(((int64_t)speed_2_20_ * (int32_t)(s.sample_us - s.transition_us)) >> 20);
      if ((int32_t)(est - high) > 0) {
        est = high;
      } else if ((int32_t)(est - low) < 0) {
        est = low;
      }
      internal_position_ = est;
    }

    // accumulate the 32-bit internal position into 64 bits, so the user
    // facing position never wraps
    position_ += (int32_t)(internal_position_ - prev_internal_position_);
    prev_internal_position_ = internal_position_;

    prev_low_ = low;
    prev_high_ = high;
    step_ = s.step;
    prev_sample_us_ = s.sample_us;
  }

  // ---- phase calibration (optional) ----

  // learn the relative phase sizes from the transition timing. Feed with the
  // same samples as update(), at a rate high enough to see every step
  // (steps slower than 20ms are ignored)
  void updateCalibration(const Sample &s) {
    if (s.step == calib_last_step_) {
      return;
    }

    uint32_t delta;
    if (calib_last_us_ == 0) {
      delta = 0;
    } else {
      delta = s.transition_us - calib_last_us_;
    }
    const int32_t steps = (int32_t)(calib_last_step_ - s.step);

    calib_last_step_ = s.step;
    calib_last_us_ = s.transition_us;

    // skipped steps or too-slow steps can not be used (and invalidate the
    // partial measurement)
    if (steps > 1 || steps < -1 || delta > 20000 || delta == 0) {
      for (int i = 0; i < 4; i++) calib_data_[i] = 0;
      return;
    }

    if (s.forward) {
      calib_data_[(s.step - 1) & 3] = delta;
    } else {
      calib_data_[(s.step + 1) & 3] = delta;
    }

    // wait until we have a measure of all 4 phases
    if (calib_data_[0] == 0 || calib_data_[1] == 0 || calib_data_[2] == 0 || calib_data_[3] == 0) {
      return;
    }

    // accumulate, rescaling to avoid overflow
    bool need_rescale = false;
    for (int i = 0; i < 4; i++) {
      calib_sum_[i] += calib_data_[i];
      calib_data_[i] = 0;
      if (calib_sum_[i] > 2500000) need_rescale = true;
    }

    uint32_t total = 0;
    for (int i = 0; i < 4; i++) {
      if (need_rescale) calib_sum_[i] >>= 1;
      total += calib_sum_[i];
    }
    calib_count_++;

    // don't use the first measurements, they may still carry a big bias
    if (calib_count_ < 32) {
      return;
    }

    // scale the phase sizes to a total of 256 substeps per 4 steps
    calibration_data_[0] = 0;
    calibration_data_[1] = (calib_sum_[0] * 256 + total / 2) / total;
    calibration_data_[2] = ((calib_sum_[0] + calib_sum_[1]) * 256 + total / 2) / total;
    calibration_data_[3] = ((calib_sum_[0] + calib_sum_[1] + calib_sum_[2]) * 256 + total / 2) / total;
  }

  bool calibrationReady() const {
    return calib_count_ >= 128;
  }

  void resetCalibration() {
    for (int i = 0; i < 4; i++) {
      calib_sum_[i] = 0;
      calib_data_[i] = 0;
    }
    calib_count_ = 0;
    calib_last_us_ = 0;
    calib_last_step_ = 0;
  }

  // ---- outputs ----

  // position in substeps (64 substeps per quadrature step), 64-bit
  int64_t position() const {
    return position_;
  }

  // speed in substeps per second
  int32_t speed() const {
    return speed_;
  }

  bool stopped() const {
    return stopped_;
  }

  // overwrite the current position (e.g. zeroing on a limit switch)
  void setPosition(int64_t p) {
    position_ = p;
  }

  uint32_t rawStep() const {
    return step_;
  }

private:
  // substep position of the transition into "step"
  uint32_t stepStartPos(uint32_t step) const {
    return ((step << 6) & 0xFFFFFF00u) | calibration_data_[step & 3];
  }

  // speed in "substeps per 2^20 us" from substep and time deltas
  static int32_t calcSpeed(int32_t delta_substep, int32_t delta_us) {
    if (delta_us <= 0) delta_us = 1;  // guard for very high sampling rates
    return (int32_t)(((int64_t)delta_substep << 20) / delta_us);
  }

  // configuration
  uint32_t calibration_data_[4];  // relative phase start offsets (0..255)
  uint32_t idle_timeout_us_ = kDefaultIdleTimeoutUs;

  // estimator state
  uint32_t step_ = 0;
  uint32_t prev_sample_us_ = 0;
  uint32_t prev_trans_pos_ = 0, prev_trans_us_ = 0;
  uint32_t prev_low_ = 0, prev_high_ = 0;
  uint32_t internal_position_ = 0, prev_internal_position_ = 0;
  int64_t position_ = 0;
  int32_t speed_ = 0;
  int32_t speed_2_20_ = 0;
  bool stopped_ = true;

  // calibration state
  uint32_t calib_last_us_ = 0, calib_last_step_ = 0;
  uint32_t calib_count_ = 0;
  uint32_t calib_sum_[4] = {}, calib_data_[4] = {};
};

}  // namespace substep_encoder
