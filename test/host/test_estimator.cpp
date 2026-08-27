// Host-side unit tests for substep_encoder::Estimator.
// Build and run with: make -C test/host run  (any C++17 compiler)

#include <cassert>
#include <cinttypes>
#include <initializer_list>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "substep_encoder_estimator.h"

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

int main() {
  test_rate_independence();
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
