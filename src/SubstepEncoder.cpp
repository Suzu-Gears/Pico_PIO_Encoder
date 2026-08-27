/*
  SubstepEncoder - high resolution quadrature encoder for RP2040/RP2350

  PIO glue: allocation of PIO blocks / state machines, program setup and
  raw sample reading. The estimation logic lives in
  substep_encoder_estimator.h.

  Based on PicoEncoder by Paulo Marques, Pedro Pereira, Paulo Costa
  (BSD 2-clause). Distributed under the BSD 2-clause license, see LICENSE.
*/

#include "SubstepEncoder.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

#include "substep_encoder.pio.h"

#ifndef NUM_PIOS
#define NUM_PIOS 2
#endif

using substep_encoder::Sample;

// ---------------------------------------------------------------------------
// PIO block / state machine allocation
//
// The PIO program uses all 32 instructions of a block, so a block that hosts
// it can not host anything else. The first encoder claims a whole free PIO
// block (all state machines, to keep other libraries out); following
// encoders fill its remaining state machines before claiming another block.
// ---------------------------------------------------------------------------

namespace {

struct PioSlot {
  PIO pio;
  uint used_sms;
};

PioSlot pio_slots[NUM_PIOS];
int pio_slot_count = 0;

PIO pio_instance(int i) {
  switch (i) {
    case 0: return pio0;
    case 1: return pio1;
#if NUM_PIOS >= 3
    case 2: return pio2;
#endif
    default: return nullptr;
  }
}

// claim a whole PIO block and load the program. Returns false if the block
// is not fully free
bool claim_whole_pio(PIO pio) {
  if (!pio_can_add_program(pio, &substep_encoder_program)) {
    return false;
  }
  for (uint i = 0; i < NUM_PIO_STATE_MACHINES; i++) {
    if (pio_sm_is_claimed(pio, i)) {
      return false;
    }
  }
  for (uint i = 0; i < NUM_PIO_STATE_MACHINES; i++) {
    pio_sm_claim(pio, i);
  }
  pio_add_program(pio, &substep_encoder_program);
  return true;
}

// find a state machine for a new encoder, optionally on a specific PIO
int allocate_sm(PIO forced, PIO *out_pio, uint *out_sm) {
  // reuse a block we already own
  for (int i = 0; i < pio_slot_count; i++) {
    if (forced != nullptr && pio_slots[i].pio != forced) {
      continue;
    }
    if (pio_slots[i].used_sms < NUM_PIO_STATE_MACHINES) {
      *out_pio = pio_slots[i].pio;
      *out_sm = pio_slots[i].used_sms++;
      return 0;
    }
  }

  // claim a new block
  if (forced != nullptr) {
    if (!claim_whole_pio(forced)) {
      return -1;
    }
    pio_slots[pio_slot_count] = { forced, 1 };
    pio_slot_count++;
    *out_pio = forced;
    *out_sm = 0;
    return 0;
  }

  for (int i = 0; i < NUM_PIOS; i++) {
    PIO pio = pio_instance(i);
    bool already_ours = false;
    for (int j = 0; j < pio_slot_count; j++) {
      if (pio_slots[j].pio == pio) already_ours = true;
    }
    if (already_ours) {
      continue;  // ours but full, checked above
    }
    if (claim_whole_pio(pio)) {
      pio_slots[pio_slot_count] = { pio, 1 };
      pio_slot_count++;
      *out_pio = pio;
      *out_sm = 0;
      return 0;
    }
  }
  return -1;
}

// initialize the PIO state machine to track the encoder on pin_A / pin_A+1
void program_init(PIO pio, uint sm, uint pin_A) {
  pio_gpio_init(pio, pin_A);
  pio_gpio_init(pio, pin_A + 1);
  pio_sm_set_consecutive_pindirs(pio, sm, pin_A, 2, false);

  pio_sm_config c = substep_encoder_program_get_default_config(0);
  sm_config_set_in_pins(&c, pin_A);
  // shift to left, auto-push at 32 bits
  sm_config_set_in_shift(&c, false, true, 32);
  sm_config_set_out_shift(&c, true, false, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
  // run at sysclk for maximum time resolution
  sm_config_set_clkdiv_int_frac(&c, 1, 0);
  // "MOV PC, ~STATUS" pushes data only when the RX FIFO has space
  sm_config_set_mov_status(&c, STATUS_RX_LESSTHAN, 1);

  pio_sm_init(pio, sm, 0, &c);

  // seed the state machine registers from the current pin state. The
  // encoder may step during this, but a single step is handled correctly
  // once the state machine starts. Disable interrupts to be safe
  const uint32_t ints = save_and_disable_interrupts();

  const uint32_t pin_state = (gpio_get_all() >> pin_A) & 3;

  // the lower 2 bits of OSR must hold the negated pin state
  pio_sm_exec(pio, sm, pio_encode_set(pio_y, (~pin_state) & 0x1F));
  pio_sm_exec(pio, sm, pio_encode_mov(pio_osr, pio_y));

  // map the phase to a step count so that the lower 2 bits of Y match the
  // pin state 1:1 (simplifies phase size compensation)
  uint32_t position;
  switch (pin_state) {
    case 0: position = 0; break;
    case 1: position = 3; break;
    case 2: position = 1; break;
    default: position = 2; break;
  }
  pio_sm_exec(pio, sm, pio_encode_set(pio_y, position));

  pio_sm_set_enabled(pio, sm, true);

  restore_interrupts(ints);
}

}  // namespace

// ---------------------------------------------------------------------------
// SubstepEncoder
// ---------------------------------------------------------------------------

int SubstepEncoder::begin(uint firstPin, bool pullUp) {
  return begin(firstPin, nullptr, pullUp);
}

int SubstepEncoder::begin(uint firstPin, PIO pio, bool pullUp) {
  if (allocate_sm(pio, &pio_, &sm_) < 0) {
    return -1;
  }

  if (pullUp) {
    gpio_set_pulls(firstPin, true, false);
    gpio_set_pulls(firstPin + 1, true, false);
  }

  program_init(pio_, sm_, firstPin);

  clocks_per_us_ = (clock_get_hz(clk_sys) + 500000) / 1000000;

  Sample s;
  readSample(&s);
  estimator_.reset(s);
  last_refresh_us_ = s.sample_us;

  return 0;
}

void SubstepEncoder::readSample(Sample *s) {
  uint32_t step = 0, cycles_raw = 0, us;

  // drain the RX FIFO and keep the freshest (cycles, step) pair. Each pair
  // is self-contained (cumulative step count + time since last transition),
  // so draining never loses information. Interrupts are disabled so there
  // is no gap between the PIO data and the timestamp
  const int pairs = pio_sm_get_rx_fifo_level(pio_, sm_) >> 1;
  const uint32_t ints = save_and_disable_interrupts();
  for (int i = 0; i < pairs + 1; i++) {
    cycles_raw = pio_sm_get_blocking(pio_, sm_);
    step = pio_sm_get_blocking(pio_, sm_);
  }
  us = time_us_32();
  restore_interrupts(ints);

  // the PIO sets the cycle counter to 0 on an incrementing transition and
  // to 2^31 on a decrementing one, then decrements it every 13-clock loop.
  // That encodes both the direction and the time of the last transition
  int32_t cycles = (int32_t)cycles_raw;
  bool forward;
  if (cycles < 0) {
    cycles = -cycles;
    forward = true;
  } else {
    cycles = (int32_t)(0x80000000u - cycles_raw);
    forward = false;
  }

  s->step = step;
  s->sample_us = us;
  s->transition_us = us - (uint32_t)(((int64_t)cycles * 13) / clocks_per_us_);
  s->forward = forward;
}

void SubstepEncoder::refresh() {
  Sample s;
  readSample(&s);
  estimator_.update(s);
  if (auto_calibrate_) {
    estimator_.updateCalibration(s);
  }
  last_refresh_us_ = s.sample_us;
}

void SubstepEncoder::maybeRefresh() {
  if ((uint32_t)(time_us_32() - last_refresh_us_) >= min_refresh_interval_us_) {
    refresh();
  }
}

int64_t SubstepEncoder::position() {
  maybeRefresh();
  return estimator_.position();
}

int32_t SubstepEncoder::speed() {
  maybeRefresh();
  return estimator_.speed();
}

bool SubstepEncoder::stopped() {
  maybeRefresh();
  return estimator_.stopped();
}

SubstepEncoder::Snapshot SubstepEncoder::read() {
  maybeRefresh();
  return { estimator_.position(), estimator_.speed(), last_refresh_us_ };
}

void SubstepEncoder::resetPosition(int64_t to) {
  estimator_.setPosition(to);
}
