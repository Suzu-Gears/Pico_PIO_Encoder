/*
  Pico_PIO_Encoder - high resolution quadrature encoder for RP2040/RP2350

  Reads a quadrature encoder with a PIO program that counts steps and
  timestamps transitions entirely in hardware (zero CPU load), and combines
  both into a high resolution position and speed estimate.

  Key properties:
   - getters acquire on demand; velocity has a separate update interval; no
     background timer is installed
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
#include "substep_encoder_index.h"
#include "substep_encoder_published.h"

// internal GPIO IRQ dispatcher for index inputs, do not call directly
void substep_encoder_index_irq_dispatch();

class Pico_PIO_Encoder {
public:
  Pico_PIO_Encoder() = default;
  ~Pico_PIO_Encoder();
  Pico_PIO_Encoder(const Pico_PIO_Encoder &) = delete;
  Pico_PIO_Encoder &operator=(const Pico_PIO_Encoder &) = delete;
  enum class Mode : uint8_t { None, Consecutive, NonConsecutive };
  enum class Pull : uint8_t { None, Up, Down };
  enum Status : uint32_t {
    Initialized = 1u << 0,
    VelocityValid = 1u << 1,
    VelocityWarmingUp = 1u << 2,
    Stale = 1u << 3,
    UpdateLate = 1u << 4,
    SpeedSaturated = 1u << 5,
    PositionSaturated = 1u << 6,
    ReadFailed = 1u << 7, // last foreground acquisition failed; held data
    PioFault = 1u << 8   // FIFO timeout; end()/begin...() required
  };
  // each quadrature step is divided into 64 substeps; a 4x-counted encoder
  // with N pulses per revolution gives N * 4 * 64 substeps per revolution
  static constexpr int kSubstepsPerStep = substep_encoder::Estimator::kSubstepsPerStep;

  // One acquisition plus the most recent independently updated speed.
  struct Snapshot {
    int64_t position;       // quadrature steps
    float speed;            // steps per second
    uint32_t timestamp_us;  // acquisition time
    uint32_t speed_timestamp_us;  // last velocity update (or stop detection)
    int64_t position_substeps;   // interpolated position
    bool stopped;
    uint32_t status;
    uint32_t sequence;       // publication counter; preserved across end/begin
    uint64_t timestamp_us64; // acquisition time without the 32-bit wrap
    uint32_t max_age_us;     // 0 disables freshness/deadline diagnostics
    Mode mode = Mode::None;
    uint8_t pin_a = 0, pin_b = 0, state_machines = 0;
    uint32_t read_failures = 0, read_retries = 0;
    bool has(Status flag) const { return (status & flag) != 0; }
    bool positionValid() const { return has(Initialized) && !has(Stale) && !has(ReadFailed) && !has(PioFault) && !has(PositionSaturated); }
    bool speedValid() const { return has(Initialized) && has(VelocityValid) && !has(Stale) && !has(ReadFailed) && !has(PioFault) && !has(SpeedSaturated); }
  };

  // Explicit wiring choices. No automatic fallback or pin reordering by the
  // caller: A/B order determines sign consistently in both modes.
  // Consecutive requires adjacent GPIOs and uses 1 SM (4 encoders/block).
  // NonConsecutive accepts any distinct A/B in one GPIO window and uses
  // 2 SMs (2 encoders/block); adjacent pins are also permitted in this mode.
  // Both programs occupy 32 instructions; modes cannot share a PIO block.
  // Both input pins use the selected internal pull; default is Pull::Up.
  // Pull::None disables both pulls, including settings from an earlier begin.
  // RP2350 A2 E9 can prevent a weak pull-down from reaching LOW; see README.
  // Return: 0 success, -1 no compatible PIO resources, -2 invalid pins/mode/pull
  // or already begun, -3 initial acquisition failed (resources returned).
  int beginConsecutive(uint pinA, uint pinB, Pull pull = Pull::Up);
  int beginConsecutive(uint pinA, uint pinB, PIO pio, Pull pull = Pull::Up);
  int beginNonConsecutive(uint pinA, uint pinB, Pull pull = Pull::Up);
  int beginNonConsecutive(uint pinA, uint pinB, PIO pio, Pull pull = Pull::Up);
  Mode mode() const { return latest().mode; }
  static const char *beginErrorMessage(int result) {
    switch(result) {
      case 0: return "OK";
      case -1: return "No free PIO resources for this mode and GPIO window";
      case -2: return "Invalid pins/PIO/pull or already begun; consecutive mode requires adjacent GPIOs";
      case -3: return "PIO acquisition failed during initialization";
      default: return "Unknown initialization error";
    }
  }

  // Idempotent; false if called on a different core from successful begin.
  // Stop foreground/IRQ access on the owner before end/destruction. Other
  // cores may continue latest() until the object's lifetime ends.
  bool end();
  bool initialized() const { return latest().has(Initialized); }
  // The ONLY encoder read API allowed on another core: copies published
  // data under a short IRQ-safe spinlock; never drains the PIO FIFO.
  Snapshot latest() const;
  static void updateFreshness(Snapshot &snapshot, uint64_t now_us);
  // Acquisition deadline and reader freshness limit. Default 10ms, 0 off.
  void setMaxSampleAgeUs(uint32_t us) { max_sample_age_us_ = us; }

  // ---- readings (self-refreshing, subject to documented counter limits) ----
  // Default units are quadrature steps (4x counting: PPR x 4 steps per
  // revolution, e.g. 400 for a 100 PPR encoder). The estimator still works
  // in substeps internally, so speed keeps its low-speed resolution as a
  // fractional value. Use the *Substeps() variants for the full 1/64-step
  // resolution.

  // Measured quadrature count, independent of interpolation and speed.
  int64_t position();

  // speed in steps per second (fractional)
  float speed();

  // Interpolated position, 64 substeps per step (not measured resolution).
  int64_t positionSubsteps();

  // full resolution speed in substeps per second
  int32_t speedSubsteps();

  // true if no transition has arrived for the idle timeout
  bool stopped();

  // Position and held speed, with separate timestamps.
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

  // Velocity updates only when this interval has elapsed at an acquisition.
  // Default 0: every acquisition. Set 10000 for 100Hz velocity with 1kHz
  // position reads. No background timer; missed deadlines are not replayed.
  // Stop detection still runs on every acquisition.
  void setSpeedUpdateIntervalUs(uint32_t us) {
    estimator_.setSpeedUpdateIntervalUs(us);
  }
  // if no transition arrives for this long, speed snaps to zero (the
  // estimate already decays towards zero before that). Default 50ms; raise
  // it to track extremely slow movement, lower it to detect stops faster
  void setIdleTimeoutUs(uint32_t us) {
    estimator_.setIdleTimeoutUs(us);
  }

  // ---- index input: Z phase, limit switch, home sensor (optional) ----
  // Captures the PIO count in the GPIO ISR. Resolution is one quadrature
  // step; latency includes interrupt dispatch and FIFO reading. This is a
  // software capture, not an edge-synchronous hardware latch.
  // All methods on an indexed instance must run on its IRQ core. All index
  // pins managed by this library must be attached on that same core.
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
  // Returns 0 on success, -1 for a conflicting registration/core, -2 when
  // not begun, invalid pull, or unsupported pin (index must be below GPIO32).
  // The index input has its own pull selection, defaulting to Pull::Up.

  int attachIndex(uint pin, bool onRisingEdge = true, Pull pull = Pull::Up,
                  uint32_t debounceUs = 0);
  void detachIndex();

  // true once at least one index event has been latched
  bool indexSeen();

  // number of index events so far (counted in the interrupt, always fresh)
  uint32_t indexCount() const;

  // latched position of the most recent index event, in steps
  int64_t lastIndexPosition();

  // one-shot homing: when the next index event arrives, shift the position
  // reference so that the latched point equals positionAtIndex (in steps)
  void zeroOnNextIndex(int64_t positionAtIndex = 0);

  // true until the event armed by zeroOnNextIndex() has been processed
  bool zeroPending();

  // distance in steps between the two most recent index events (0 until
  // two events have been latched). For a Z phase this should equal +/- one
  // revolution in steps; deviations may also reflect ISR capture latency
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
    rad_per_substep_ = steps ? (float)(6.283185307179586 / (double)substeps_per_rev_) : 0;
    deg_per_substep_ = steps ? (float)(360.0 / (double)substeps_per_rev_) : 0;
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
  // The int64 substep position is an interpolated estimate. Converting it straight
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
  Mode mode_ = Mode::None;
  uint pin_a_ = 0, pin_b_ = 0;
  bool invert_raw_ = false, pio_fault_ = false, read_failed_ = false;
  uint32_t pair_offset_ = 0, read_failures_ = 0, read_retries_ = 0;
  uint32_t system_clock_hz_ = 1;
  uint32_t last_refresh_us_ = 0;
  uint32_t min_refresh_interval_us_ = 100;
  bool auto_calibrate_ = false;
  int owner_core_ = -1;
  uint32_t sequence_ = 0;
  uint32_t max_sample_age_us_ = 10000;
  uint64_t last_refresh_us64_ = 0;
  bool update_late_ = false;
  substep_encoder::Published<Snapshot> published_;
  int64_t substeps_per_rev_ = 0;
  float rad_per_substep_ = 0.0f;
  float deg_per_substep_ = 0.0f;

  bool index_attached_ = false;
  uint index_pin_ = 0;
  uint32_t index_debounce_us_ = 0;
  substep_encoder::IndexCapture index_capture_;
  uint32_t index_processed_count_ = 0;
  int64_t last_index_position_ = 0, prev_index_position_ = 0;
  bool index_latched_ = false, index_have_pair_ = false;
  int beginPins(uint pinA, uint pinB, Mode mode, PIO pio, Pull pull);
  bool readSample(substep_encoder::Sample *s, uint64_t *timestamp64 = nullptr);
  void maybeRefresh();
  void processIndexEvents();
  void onIndexIrq();
  void publish();

  friend void substep_encoder_index_irq_dispatch();
};
