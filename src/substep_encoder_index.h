// Index bookkeeping, used under the owning core's interrupt lock.
#pragma once
#include <cstdint>

namespace substep_encoder {
class IndexCapture {
public:
  void reset() { *this = IndexCapture(); }
  bool record(uint32_t step, uint32_t us, uint32_t debounce_us) {
    if (seen_ && debounce_us && uint32_t(us - time_us_) < debounce_us) return false;
    previous_step_ = latest_step_;
    have_pair_ = seen_;
    latest_step_ = step;
    time_us_ = us;
    count_++;
    seen_ = true;
    if (armed_ && !zero_captured_) {
      zero_step_ = step;
      zero_captured_ = true;
    }
    return true;
  }
  void arm(int64_t target) {
    target_ = target;
    zero_captured_ = false;
    armed_ = true;
  }
  void completeZero() { armed_ = zero_captured_ = false; }
  bool seen() const { return seen_; }
  bool havePair() const { return have_pair_; }
  bool armed() const { return armed_; }
  bool zeroCaptured() const { return zero_captured_; }
  uint32_t count() const { return count_; }
  uint32_t latestStep() const { return latest_step_; }
  uint32_t previousStep() const { return previous_step_; }
  uint32_t zeroStep() const { return zero_step_; }
  int64_t target() const { return target_; }
private:
  uint32_t count_ = 0, time_us_ = 0;
  uint32_t latest_step_ = 0, previous_step_ = 0, zero_step_ = 0;
  int64_t target_ = 0;
  bool seen_ = false, have_pair_ = false, armed_ = false, zero_captured_ = false;
};
}  // namespace substep_encoder
