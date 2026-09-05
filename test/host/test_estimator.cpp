// Host-side unit tests for substep_encoder::Estimator.
// Build and run with: make -C test/host run  (any C++17 compiler)

#include <cassert>
#include <cinttypes>
#include <initializer_list>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "substep_encoder_estimator.h"
#include "substep_encoder_index.h"

using substep_encoder::Estimator;
using substep_encoder::Sample;

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      failures++;                                                       \
    }                                                                   \
  } while (0)

#define CHECK_NEAR(value, expected, tol)                                       \
  do {                                                                         \
    const double v_ = (double)(value), e_ = (double)(expected);                \
    if (std::fabs(v_ - e_) > (tol)) {                                          \
      std::printf("FAIL %s:%d: %s = %.1f, expected %.1f +/- %.1f\n", __FILE__, \
                  __LINE__, #value, v_, e_, (double)(tol));                    \
      failures++;                                                              \
    }                                                                          \
  } while (0)

// Simulates an encoder moving at a constant rate of one step per period_us,
// starting to move at time t0 (the first transition happens at t0).
struct ConstSpeedSim {
  uint64_t t0;
  uint32_t step0;
  uint32_t period_us;
  bool forward;

  Sample at(uint64_t now) const {
    const uint64_t transitions = (now >= t0) ? ((now - t0) / period_us + 1) : 0;
    Sample s;
    s.step = forward ? step0 + (uint32_t)transitions : step0 - (uint32_t)transitions;
    s.sample_us = (uint32_t)now;
    s.transition_us =
        (transitions > 0) ? (uint32_t)(t0 + (transitions - 1) * period_us) : (uint32_t)now;
    s.forward = forward;
    return s;
  }
};

// Run a constant-speed simulation, sampling every sample_period_us for
// duration_us, and return the estimator for inspection.
static Estimator run_const_speed(const ConstSpeedSim &sim, uint64_t start_us,
                                 uint64_t duration_us, uint64_t sample_period_us) {
  Estimator est;
  est.reset(sim.at(start_us));
  for (uint64_t t = start_us + sample_period_us; t <= start_us + duration_us;
       t += sample_period_us) {
    est.update(sim.at(t));
  }
  return est;
}

// ---------------------------------------------------------------------------
// 1. Rate independence: identical scenario sampled at 1kHz and at 100Hz must
//    produce the same speed and position (this is the core improvement over
//    sample-count based idle detection)
// ---------------------------------------------------------------------------
static void test_rate_independence() {
  // 400 steps/s = 25600 substeps/s. At 1kHz sampling this is 0.4 steps per
  // sample, which the old 3-sample idle logic would misread as "stopped"
  ConstSpeedSim sim{ 10000, 1000, 2500, true };

  Estimator fast = run_const_speed(sim, 5000, 1000000, 1000);   // 1kHz
  Estimator slow = run_const_speed(sim, 5000, 1000000, 10000);  // 100Hz

  CHECK_NEAR(fast.speed(), 25600, 300);
  CHECK_NEAR(slow.speed(), 25600, 300);
  CHECK_NEAR(fast.position(), slow.position(), 64);  // within one step

  // ~400 steps in ~1s => ~25600 substeps travelled
  CHECK_NEAR(fast.position(), 25600, 2 * 64);

  std::printf("rate_independence: fast speed=%d slow speed=%d fast pos=%" PRId64
              " slow pos=%" PRId64 "\n",
              fast.speed(), slow.speed(), fast.position(), slow.position());
}

// ---------------------------------------------------------------------------
// 2. Stop detection is wall-clock based: speed decays and then snaps to zero
//    after the idle timeout, at any sampling rate; position stays put
// ---------------------------------------------------------------------------
static void test_stop_detection() {
  ConstSpeedSim sim{ 10000, 1000, 2500, true };

  for (uint64_t sample_period : { 1000ull, 10000ull }) {
    Estimator est;
    est.reset(sim.at(5000));
    uint64_t t = 5000;
    Sample last = sim.at(t);
    for (; t <= 500000; t += sample_period) {
      last = sim.at(t);
      est.update(last);
    }
    CHECK(!est.stopped());
    const int64_t pos_at_stop = est.position();

    // motion stops exactly at the last observed state: keep sampling a
    // frozen encoder (only the sample timestamp advances)
    Sample frozen = last;
    for (uint64_t idle = 0; idle <= 200000; idle += sample_period) {
      frozen.sample_us = (uint32_t)(t + idle);
      est.update(frozen);
    }
    CHECK(est.stopped());
    CHECK(est.speed() == 0);
    CHECK_NEAR(est.position(), pos_at_stop, 64);
  }
}

// ---------------------------------------------------------------------------
// 3. Reverse direction: negative speed, decreasing position
// ---------------------------------------------------------------------------
static void test_reverse() {
  ConstSpeedSim sim{ 10000, 100000, 2500, false };
  Estimator est = run_const_speed(sim, 5000, 1000000, 1000);
  CHECK_NEAR(est.speed(), -25600, 300);
  CHECK_NEAR(est.position(), -25600, 2 * 64);
}

// ---------------------------------------------------------------------------
// 4. Step counter wrap: position accumulates in 64 bits and keeps counting
//    smoothly across the 32-bit wrap of the hardware step counter
// ---------------------------------------------------------------------------
static void test_step_wrap() {
  ConstSpeedSim sim{ 10000, 0xFFFFFFB0u, 2500, true };  // wraps after 80 steps
  Estimator est = run_const_speed(sim, 5000, 1000000, 1000);
  CHECK_NEAR(est.speed(), 25600, 300);
  CHECK_NEAR(est.position(), 25600, 2 * 64);  // ~400 steps forward, no glitch
}

// ---------------------------------------------------------------------------
// 5. setPosition: overwrites the reference, further motion stays relative
// ---------------------------------------------------------------------------
static void test_set_position() {
  ConstSpeedSim sim{ 10000, 1000, 2500, true };
  Estimator est;
  est.reset(sim.at(5000));
  for (uint64_t t = 6000; t <= 500000; t += 1000) {
    est.update(sim.at(t));
  }
  est.setPosition(0);
  for (uint64_t t = 501000; t <= 1000000; t += 1000) {
    est.update(sim.at(t));
  }
  // ~0.5s of motion after zeroing => ~200 steps
  CHECK_NEAR(est.position(), 200 * 64, 2 * 64);
}

// ---------------------------------------------------------------------------
// 6. Calibration: with perfectly symmetric phases the learned phase sizes
//    stay (close to) the default, and readiness is reported
// ---------------------------------------------------------------------------
static void test_calibration() {
  ConstSpeedSim sim{ 10000, 1000, 2500, true };
  Estimator est;
  est.reset(sim.at(5000));
  for (uint64_t t = 6000; t <= 3000000; t += 1000) {
    const Sample s = sim.at(t);
    est.update(s);
    est.updateCalibration(s);
  }
  CHECK(est.calibrationReady());
  const int phases = est.getPhases();
  const int p1 = phases & 0xFF, p2 = (phases >> 8) & 0xFF, p3 = (phases >> 16) & 0xFF;
  CHECK_NEAR(p1, 0x40, 2);
  CHECK_NEAR(p2, 0x40, 2);
  CHECK_NEAR(p3, 0x40, 2);
}

// ---------------------------------------------------------------------------
// 7. positionAt: reconstructing the position at an earlier instant (used by
//    the index input latch) matches the simulated ground truth
// ---------------------------------------------------------------------------
static void test_position_at() {
  ConstSpeedSim sim{ 10000, 1000, 2500, true };
  Estimator est = run_const_speed(sim, 5000, 1000000, 1000);

  // ground truth: position accumulated since reset at t=5000 is about
  // (t' - 5000) / 2500 steps of 64 substeps each
  const uint32_t t_event = 1005000 - 10000;  // 10ms before the last sample
  const double truth = (double)(t_event - 5000) / 2500.0 * 64.0;
  CHECK_NEAR(est.positionAt(t_event), truth, 96);

  // and it must differ from the current position by speed * dt
  CHECK_NEAR(est.position() - est.positionAt(t_event), est.speed() * 0.01, 8);
}

static void test_independent_velocity_schedule() {
  Estimator frequent, sparse;
  frequent.setSpeedUpdateIntervalUs(10000);
  sparse.setSpeedUpdateIntervalUs(10000);
  Sample initial{3, 0, 0, true};
  frequent.reset(initial);
  sparse.reset(initial);
  uint32_t step = 3, transition = 0;
  // Accelerating motion: the period changes at 200ms. Both estimators see
  // identical velocity deadlines, but only one sees intermediate positions.
  for (uint32_t t = 1000; t <= 400000; t += 1000) {
    if (t <= 200000 ? t % 5000 == 0 : t % 2000 == 0) {
      ++step;
      transition = t - 300;
    }
    const Sample s{step, t, transition, true};
    const int32_t held = frequent.speed();
    frequent.update(s);
    CHECK(frequent.measuredPosition() == int64_t(step - 3));
    if (t % 10000 == 0) {
      sparse.update(s);
      CHECK(frequent.speed() == sparse.speed());
      CHECK(frequent.speedTimestampUs() == t);
      CHECK(frequent.measuredPosition() == sparse.measuredPosition());
    } else {
      CHECK(frequent.speed() == held);
      CHECK(frequent.speedTimestampUs() == t / 10000 * 10000);
    }
  }
  CHECK_NEAR(frequent.speed(), 500 * 64, 2);
  frequent.setSpeedUpdateIntervalUs(20000);
  frequent.update({step + 1, 405000, 404000, true});
  CHECK(frequent.speedTimestampUs() == 400000);
  frequent.update({step + 4, 420000, 419000, true});
  CHECK(frequent.speedTimestampUs() == 420000);
}

static void test_initial_phase_bounds() {
  for (uint32_t phase = 0; phase < 4; ++phase) {
    for (bool forward : {true, false}) {
      Estimator est;
      est.reset({phase, 0, 0, forward});
      est.update({forward ? phase + 1 : phase - 1, 1000, 900, forward});
      CHECK(std::abs(est.speed()) <= 71112);  // 64 substeps / 900us
      CHECK(est.measuredPosition() == (forward ? 1 : -1));
    }
  }
}

static void test_velocity_deadline_jitter() {
  Estimator est;
  est.setSpeedUpdateIntervalUs(10000);
  est.reset({0, 0, 0, true});
  uint32_t updates = 0, previous = 0;
  for (uint32_t n = 1; n <= 1000; ++n) {
    const uint32_t t = n * 1000 + (n % 3 == 0 ? -1 : 1);
    est.update({0, t, 0, true});
    if (est.speedTimestampUs() != previous) {
      ++updates;
      previous = est.speedTimestampUs();
      CHECK(t >= updates * 10000);
      CHECK(t < updates * 10000 + 1002);
    }
  }
  CHECK(updates == 100);
}

static void test_sparse_stop_and_restart() {
  Estimator dense, sparse;
  dense.reset({0, 0, 0, true});
  sparse.reset({0, 0, 0, true});
  dense.update({1, 1000, 900, true});
  sparse.update({1, 1000, 900, true});
  dense.update({2, 2000, 1900, true});
  dense.update({2, 100000, 1900, true});
  sparse.update({2, 100000, 1900, true});
  CHECK(dense.stopped() && sparse.stopped());
  CHECK(dense.speed() == 0 && sparse.speed() == 0);
  CHECK(dense.measuredPosition() == 2 && sparse.measuredPosition() == 2);
  sparse.update({3, 101000, 100900, true});
  CHECK(!sparse.stopped());
  sparse.update({4, 102000, 101900, true});
  CHECK_NEAR(sparse.speed(), 64000, 2);

  // Timeout must override a held velocity before the next velocity tick.
  sparse.setSpeedUpdateIntervalUs(100000);
  sparse.update({4, 151900, 101900, true});
  CHECK(sparse.stopped() && sparse.speed() == 0);
  CHECK(sparse.speedTimestampUs() == 151900);
}

static void test_very_slow_and_timestamp_wrap() {
  ConstSpeedSim slow{10000, 0, 200000, true};
  Estimator est;
  est.setIdleTimeoutUs(500000);
  est.setSpeedUpdateIntervalUs(10000);
  est.reset(slow.at(0));
  for (uint32_t t = 1000; t <= 1000000; t += 1000) est.update(slow.at(t));
  CHECK(!est.stopped());
  CHECK_NEAR(est.speed(), 5 * 64, 2);

  ConstSpeedSim wrap{0xFFFFFF00ULL, 0xFFFFFFFEu, 2500, true};
  Estimator wrapped;
  wrapped.setSpeedUpdateIntervalUs(10000);
  wrapped.reset(wrap.at(0xFFFFF000ULL));
  for (uint64_t t = 0xFFFFF000ULL + 1000; t < 0x100040000ULL; t += 1000)
    wrapped.update(wrap.at(t));
  CHECK_NEAR(wrapped.speed(), 25600, 2);
  CHECK(wrapped.measuredPosition() > 100);
}

static void test_measured_count_and_reversal() {
  Estimator est;
  est.reset({0xFFFFFFFEu, 0, 0, true});
  est.update({1, 1000, 900, true});
  CHECK(est.measuredPosition() == 3);
  est.update({0xFFFFFFFFu, 2000, 1900, false});
  CHECK(est.measuredPosition() == 1);
  CHECK(est.speed() <= 0);
  est.setPosition(640);
  CHECK(est.measuredPosition() == 10 && est.position() == 640);
  est.update({0xFFFFFFFEu, 3000, 2900, false});
  CHECK(est.measuredPosition() == 9);
  est.shiftPositionSteps(-9);
  CHECK(est.measuredPosition() == 0);

  // The old 32-bit substep accumulator lost large gaps; the measured
  // counter is extended before multiplying by 64 now.
  est.reset({0, 0, 0, true});
  est.update({40000000, 1000000, 900000, true});
  CHECK(est.measuredPosition() == 40000000);
  CHECK(est.position() > INT32_MAX);
}

static void test_zero_time_and_jitter() {
  Estimator est;
  est.reset({3, 1000, 900, true});
  est.update({4, 1000, 1000, true});
  est.update({5, 1000, 1000, true});
  CHECK(est.measuredPosition() == 2);
  est.update({5, 51000, 51000, true}); // fake PIO timestamp drift, no edge
  CHECK(est.stopped());
  CHECK(est.speed() == 0);
  est.update({5, 1001, 1001, true}); // a full 32-bit timer wrap later, no edge
  CHECK(est.stopped());            // must not restart itself at timer rollover
}

static void test_index_capture() {
  substep_encoder::IndexCapture index;
  Estimator est;
  est.reset({0xFFFFFF00u, 0, 0, true});
  index.record(0xFFFFFF10u, 1000, 0); // old event before arming
  index.arm(0);
  index.record(0xFFFFFFF0u, 2000, 0); // must be the homing event
  index.record(0x00000180u, 3000, 0); // +400 counts
  index.record(0x00000310u, 4000, 0); // +400 counts, no foreground poll
  est.update({0x00000320u, 100000, 5000, true}); // motor has stopped
  CHECK(est.stopped());
  CHECK(index.count() == 4 && index.havePair());
  CHECK(index.zeroStep() == 0xFFFFFFF0u);
  est.shiftPositionSteps(index.target() - est.measuredPositionAtStep(index.zeroStep()));
  index.completeZero();
  CHECK(!index.armed());
  CHECK(est.measuredPosition() == 816);
  CHECK(est.measuredPositionAtStep(index.latestStep()) == 800);
  CHECK(substep_encoder::counterDelta(index.latestStep(), index.previousStep()) == 400);
  index.reset();
  CHECK(!index.seen() && !index.havePair() && index.count() == 0);
  CHECK(index.record(1, 0xFFFFFFF0u, 100));
  CHECK(!index.record(2, 0x00000010u, 100));
  CHECK(index.record(3, 0x00000060u, 100));
  CHECK(index.count() == 2);
}

static void test_diagnostics_and_saturation() {
  Estimator est;
  est.reset({0, 0, 0, true});
  CHECK(!est.velocityReady());
  est.update({0, 49999, 0, true});
  CHECK(!est.velocityReady());
  est.update({0, 50000, 0, true});
  CHECK(est.velocityReady() && est.stopped());
  est.update({1, 51000, 51000, true});
  CHECK(!est.velocityReady());
  est.update({2, 52000, 52000, true});
  CHECK(est.velocityReady() && !est.speedSaturated());
  est.update({1, 53000, 53000, false});
  CHECK(!est.velocityReady());
  est.update({0, 54000, 54000, false});
  CHECK(est.velocityReady());
  est.reset({0, 0, 0, true});
  est.update({100000, 1, 1, true});
  est.update({200000, 2, 2, true});
  CHECK(est.speedSaturated());
  est.update({200000, 100000, 2, true});
  CHECK(!est.speedSaturated() && est.stopped());
  est.setPositionSteps(INT64_MAX);
  CHECK(est.measuredPosition() == INT64_MAX && est.positionSaturated());
  est.update({200001, 100001, 100001, true});
  CHECK(est.measuredPosition() == INT64_MAX);
  est.reset({0, 0, 0, true});
  est.setPositionSteps(INT64_MIN);
  CHECK(est.measuredPosition() == INT64_MIN && est.positionSaturated());
  est.update({0xFFFFFFFFu, 1000, 1000, false});
  CHECK(est.measuredPosition() == INT64_MIN);
  est.setPositionSteps(0);
  CHECK(est.measuredPosition() == 0 && !est.positionSaturated());
  est.update({0xFFFFFFFEu, 2000, 2000, false});
  CHECK(est.measuredPosition() == -1 && !est.positionSaturated());
  est.reset({0, 0, 0, true});
  est.setPositionSteps(1LL << 40);
  CHECK(est.measuredPosition() == (1LL << 40));
  CHECK(!est.positionSaturated());
  est.update({1, 1000, 1000, true});
  CHECK(est.measuredPosition() == (1LL << 40) + 1);
  bool saturated = false;
  CHECK(substep_encoder::saturatedSubtract(0, INT64_MIN, saturated) == INT64_MAX);
  CHECK(saturated);
}

#include "substep_encoder_pins.h"
static void test_pin_modes() {
  using substep_encoder::validPins;
  using substep_encoder::pinsInWindow;
  CHECK(validPins(2,3,30,true));CHECK(validPins(3,2,30,true));
  CHECK(!validPins(2,10,30,true));CHECK(validPins(2,10,30,false));
  CHECK(validPins(2,3,30,false));CHECK(!validPins(2,2,30,false));
  CHECK(!validPins(29,30,30,true));CHECK(!validPins(UINT32_MAX,0,30,false));
  CHECK(validPins(31,32,48,true));CHECK(validPins(16,47,48,false));
  CHECK(!validPins(15,32,48,false));CHECK(!validPins(0,47,48,false));
  CHECK(!validPins(47,48,48,true));
  CHECK(pinsInWindow(16,31,0)&&pinsInWindow(16,31,16));
  CHECK(!pinsInWindow(2,3,16));CHECK(pinsInWindow(31,32,16));
}
int main() {
  test_pin_modes();
  test_diagnostics_and_saturation();
  test_velocity_deadline_jitter();
  test_independent_velocity_schedule();
  test_initial_phase_bounds();
  test_sparse_stop_and_restart();
  test_very_slow_and_timestamp_wrap();
  test_measured_count_and_reversal();
  test_zero_time_and_jitter();
  test_index_capture();
  test_rate_independence();
  test_position_at();
  test_stop_detection();
  test_reverse();
  test_step_wrap();
  test_set_position();
  test_calibration();

  if (failures == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
