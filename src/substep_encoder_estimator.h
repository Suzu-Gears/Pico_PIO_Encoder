// Phase calibration derived from PicoEncoder (BSD-2-Clause).
// Hardware-independent motion estimation and calibration.
#pragma once
#include "substep_encoder_motion.h"

namespace substep_encoder {
class Estimator : public MotionEstimator {
public:
  static constexpr int kDefaultPhases = 0x404040;
  Estimator() { setPhases(kDefaultPhases); }
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

private:
  // calibration state
  uint32_t calib_last_us_ = 0, calib_last_step_ = 0;
  uint32_t calib_count_ = 0;
  uint32_t calib_sum_[4] = {}, calib_data_[4] = {};
};

}  // namespace substep_encoder
