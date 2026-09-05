// Position and independently scheduled velocity estimation.
// Velocity bounds derived from PicoEncoder (BSD-2-Clause).
#pragma once
#include <cstdint>
#include <climits>
#include "substep_encoder_arithmetic.h"

namespace substep_encoder {

struct Sample {
  uint32_t step;
  uint32_t sample_us;
  uint32_t transition_us;
  bool forward;
};

// Modular counter difference, without implementation-defined unsigned casts.
inline int64_t counterDelta(uint32_t current, uint32_t previous) {
  const uint32_t d = current - previous;
  return d <= INT32_MAX ? int64_t(d) : int64_t(d) - 0x100000000LL;
}

class MotionEstimator {
public:
  static constexpr int kSubstepsPerStep = 64;
  static constexpr uint32_t kDefaultIdleTimeoutUs = 50000;

  void setIdleTimeoutUs(uint32_t us) { idle_timeout_us_ = us; }
  uint32_t idleTimeoutUs() const { return idle_timeout_us_; }
  // 0 preserves the old update-on-every-acquisition behavior. No background
  // timer: a due update runs at the next acquisition, without catch-up calls.
  void setSpeedUpdateIntervalUs(uint32_t us) { speed_interval_us_ = us; }
  uint32_t speedUpdateIntervalUs() const { return speed_interval_us_; }
  uint32_t speedTimestampUs() const { return speed_timestamp_us_; }
  bool velocityReady() const { return velocity_ready_; }
  bool speedSaturated() const { return speed_saturated_; }
  bool positionSaturated() const { return position_saturated_; }

  void reset(const Sample &s) {
    latest_ = s;
    count_ = measured_position_ = 0;
    velocity_ready_ = speed_saturated_ = position_saturated_ = false;
    reset_us_ = s.sample_us;
    position_ = 0;
    origin_ = -phaseOffset(s.step) - phaseWidth(s.step) / 2;
    speed_ = speed_fixed_ = 0;
    stopped_ = true;
    seen_motion_ = false;
    velocity_valid_ = false;
    velocity_sample_ = s;
    velocity_transition_pos_ = stepStart(s.step);
    velocity_low_ = stepStart(s.step);
    velocity_high_ = stepStart(s.step + 1);
    last_speed_update_us_ = speed_timestamp_us_ = s.sample_us;
  }

  void update(const Sample &s) {
    const int64_t delta = counterDelta(s.step, latest_.step);
    count_ = saturatedAdd(count_, delta, position_saturated_);
    measured_position_ = saturatedAdd(measured_position_, delta, position_saturated_);
    // PIO's reconstructed timestamp has microsecond jitter. Do not treat
    // timestamp changes alone as edges. Net-zero excursions between reads
    // cannot reliably be observed by this PIO snapshot protocol.
    const bool changed = delta != 0;
    const bool same_step = delta == 0;
    latest_.step = s.step;
    latest_.sample_us = s.sample_us;
    if (changed) {
      if (stopped_ || s.forward != latest_.forward) {
        velocity_valid_ = velocity_ready_ = false;
      }
      latest_.transition_us = s.transition_us;
      latest_.forward = s.forward;
      seen_motion_ = true;
    }
    const uint32_t age = s.sample_us - latest_.transition_us;
    const bool idle = !seen_motion_ || (!changed && stopped_) || age >= idle_timeout_us_;
    if (idle) {
      // Stop is observable on every acquisition, even between speed ticks.
      if (!stopped_ || speed_ != 0) speed_timestamp_us_ = s.sample_us;
      speed_ = speed_fixed_ = 0;
      velocity_valid_ = false;
      velocity_ready_ = seen_motion_ || uint32_t(s.sample_us - reset_us_) >= idle_timeout_us_;
      speed_saturated_ = false;
    }
    stopped_ = idle;
    const uint32_t elapsed = s.sample_us - last_speed_update_us_;
    if (elapsed >= speed_interval_us_) {
      updateVelocity(latest_);
      // Keep the deadline phase despite acquisition jitter. Skip missed
      // deadlines rather than drifting or replaying old updates.
      if (speed_interval_us_) last_speed_update_us_ += elapsed / speed_interval_us_ * speed_interval_us_;
      else last_speed_update_us_ = s.sample_us;
      speed_timestamp_us_ = s.sample_us;
    }

    // Position is always based on the 64-bit measured counter. Velocity
    // updates never determine how many physical steps have been counted.
    const int64_t low = saturatedAdd(saturatedAdd(saturatedSubsteps(count_, position_saturated_),
        phaseOffset(s.step), position_saturated_), origin_, position_saturated_);
    const int64_t high = saturatedAdd(low, phaseWidth(s.step), position_saturated_);
    if (idle) {
      // Freeze within the current interval, but still reflect movement that
      // happened and stopped during a long gap between acquisitions.
      if (!same_step) position_ = s.forward ? low : high;
    } else {
      int32_t projection_speed = speed_fixed_;
      if ((latest_.forward && projection_speed < 0) ||
          (!latest_.forward && projection_speed > 0)) projection_speed = 0;
      position_ = saturatedAdd(latest_.forward ? low : high,
                  (int64_t(projection_speed) * age) / 1048576, position_saturated_);
    }
    if (position_ < low) position_ = low;
    if (position_ > high) position_ = high;
  }

  int64_t measuredPosition() const { return measured_position_; }
  int64_t position() const { return position_; }
  int32_t speed() const { return speed_; }
  bool stopped() const { return stopped_; }
  uint32_t rawStep() const { return latest_.step; }
  uint32_t timestampUs() const { return latest_.sample_us; }

  void setPosition(int64_t substeps) {
    bool old_saturated = false;
    const int64_t low = saturatedAdd(saturatedAdd(saturatedSubsteps(count_, old_saturated),
        phaseOffset(latest_.step), old_saturated), origin_, old_saturated);
    int64_t fraction = saturatedSubtract(position_, low, old_saturated);
    if (old_saturated || fraction < 0 || fraction > phaseWidth(latest_.step))
      fraction = phaseWidth(latest_.step) / 2;
    position_saturated_ = false;
    // Rebase relative interpolation travel as well as the public count. A
    // zero reset must recover even after a previous reference saturated.
    count_ = 0;
    origin_ = saturatedSubtract(saturatedSubtract(substeps, phaseOffset(latest_.step),
        position_saturated_), fraction, position_saturated_);
    position_ = substeps;
    measured_position_ = substeps / 64;
  }
  void setPositionSteps(int64_t steps) {
    bool saturated = false;
    setPosition(saturatedSubsteps(steps, saturated));
    measured_position_ = steps;
    position_saturated_ |= saturated;
  }
  void shiftPositionSteps(int64_t steps) {
    measured_position_ = saturatedAdd(measured_position_, steps, position_saturated_);
    const int64_t shift = saturatedSubsteps(steps, position_saturated_);
    origin_ = saturatedAdd(origin_, shift, position_saturated_);
    position_ = saturatedAdd(position_, shift, position_saturated_);
  }
  int64_t measuredPositionAtStep(uint32_t step) const {
    bool ignored = false;
    return saturatedAdd(measuredPosition(), counterDelta(step, latest_.step), ignored);
  }
  // Approximation only; index capture deliberately does not use this helper.
  int64_t positionAt(uint32_t t_us) const {
    bool ignored = false;
    return saturatedSubtract(position_, int64_t(speed_) * counterDelta(latest_.sample_us, t_us) / 1000000, ignored);
  }

protected:
  uint32_t calibration_data_[4] = {0, 64, 128, 192};

private:
  int32_t phaseOffset(uint32_t step) const {
    return int32_t(calibration_data_[step & 3]) - int32_t((step & 3) * 64);
  }
  int32_t phaseWidth(uint32_t step) const {
    const uint32_t phase = step & 3;
    return int32_t(phase == 3 ? 256 : calibration_data_[phase + 1]) -
           int32_t(calibration_data_[phase]);
  }
  uint32_t stepStart(uint32_t step) const {
    return ((step << 6) & 0xFFFFFF00u) + calibration_data_[step & 3];
  }
  static int32_t calcSpeed(int64_t dx, uint32_t dt) {
    if (dt == 0) dt = 1;
    const int64_t result = dx * 1048576 / dt;
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return int32_t(result);
  }
  void updateVelocity(const Sample &s) {
    const uint32_t low = stepStart(s.step), high = stepStart(s.step + 1);
    const bool changed = s.step != velocity_sample_.step ||
                         s.forward != velocity_sample_.forward;
    if (!stopped_) {
      if (changed) {
        const uint32_t transition = s.forward ? low : high;
        if (velocity_valid_) {
          speed_fixed_ = calcSpeed(counterDelta(transition, velocity_transition_pos_),
                                  s.transition_us - velocity_sample_.transition_us);
          velocity_ready_ = true;
        }
        velocity_transition_pos_ = transition;
        velocity_sample_.transition_us = s.transition_us;
        velocity_valid_ = true;
      }
      if (velocity_valid_) {
        const int64_t before = counterDelta(velocity_sample_.transition_us,
                                            velocity_sample_.sample_us);
        const uint32_t after = s.sample_us - velocity_sample_.transition_us;
        int32_t upper, lower;
        if (before > 0 && uint64_t(before) > after) {
          upper = calcSpeed(counterDelta(velocity_transition_pos_, velocity_low_), uint32_t(before));
          lower = calcSpeed(counterDelta(velocity_transition_pos_, velocity_high_), uint32_t(before));
        } else {
          upper = calcSpeed(counterDelta(high, velocity_transition_pos_), after);
          lower = calcSpeed(counterDelta(low, velocity_transition_pos_), after);
        }
        if (speed_fixed_ > upper) speed_fixed_ = upper;
        if (speed_fixed_ < lower) speed_fixed_ = lower;
      }
      speed_ = int32_t(int64_t(speed_fixed_) * 1000000 / 1048576);
      speed_saturated_ = speed_fixed_ == INT32_MAX || speed_fixed_ == INT32_MIN;
    }
    velocity_sample_.step = s.step;
    velocity_sample_.sample_us = s.sample_us;
    velocity_sample_.forward = s.forward;
    velocity_low_ = low;
    velocity_high_ = high;
  }

  uint32_t idle_timeout_us_ = kDefaultIdleTimeoutUs;
  uint32_t speed_interval_us_ = 0;
  uint32_t last_speed_update_us_ = 0, speed_timestamp_us_ = 0;
  Sample latest_ = {}, velocity_sample_ = {};
  int64_t count_ = 0, measured_position_ = 0, position_ = 0, origin_ = -32;
  uint32_t reset_us_ = 0;
  bool velocity_ready_ = false, speed_saturated_ = false, position_saturated_ = false;
  int32_t speed_ = 0, speed_fixed_ = 0;
  uint32_t velocity_transition_pos_ = 0, velocity_low_ = 0, velocity_high_ = 64;
  bool stopped_ = true, seen_motion_ = false, velocity_valid_ = false;
};
}  // namespace substep_encoder
