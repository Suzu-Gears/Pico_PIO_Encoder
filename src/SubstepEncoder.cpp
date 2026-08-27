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
#include "hardware/irq.h"
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

// ---------------------------------------------------------------------------
// Index input IRQ dispatch
//
// The GPIO bank IRQ is shared: this raw handler only acknowledges and
// forwards events for the pins registered here, so it coexists with
// attachInterrupt() from the Arduino core and with other libraries.
// ---------------------------------------------------------------------------

struct IndexSlot {
  uint pin;
  SubstepEncoder *enc;
};

constexpr int kMaxIndexSlots = 12;
IndexSlot index_slots[kMaxIndexSlots];
int index_slot_count = 0;
bool index_dispatcher_added = false;

}  // namespace

void substep_encoder_index_irq_dispatch() {
  for (int i = 0; i < index_slot_count; i++) {
    const uint32_t events = gpio_get_irq_event_mask(index_slots[i].pin);
    if (events != 0) {
      gpio_acknowledge_irq(index_slots[i].pin, events);
      index_slots[i].enc->onIndexIrq();
    }
  }
}

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

  if (index_attached_) {
    processIndexEvents();
  }
}

// ---------------------------------------------------------------------------
// Index input
// ---------------------------------------------------------------------------

// interrupt context: only record the event time (touching the PIO here
// would race with a refresh in progress)
void SubstepEncoder::onIndexIrq() {
  const uint32_t now = time_us_32();
  if (index_debounce_us_ != 0 && index_isr_count_ != 0 &&
      (uint32_t)(now - index_isr_us_) < index_debounce_us_) {
    return;
  }
  index_isr_us_ = now;
  index_isr_count_ = index_isr_count_ + 1;
}

// called from refresh(): reconstruct the position at the latest event time
void SubstepEncoder::processIndexEvents() {
  const uint32_t ints = save_and_disable_interrupts();
  const uint32_t count = index_isr_count_;
  const uint32_t event_us = index_isr_us_;
  restore_interrupts(ints);

  if (count == index_processed_count_) {
    return;
  }
  index_processed_count_ = count;

  int64_t latched = estimator_.positionAt(event_us);
  if (zero_armed_) {
    const int64_t offset = zero_target_ - latched;
    estimator_.setPosition(estimator_.position() + offset);
    // shift the stored latch into the new coordinates so that
    // lastIndexSpacing() stays meaningful across the zeroing
    last_index_position_ += offset;
    latched = zero_target_;
    zero_armed_ = false;
  }
  if (index_latched_) {
    prev_index_position_ = last_index_position_;
  }
  last_index_position_ = latched;
  index_latch_count_++;
  index_latched_ = true;
}

int SubstepEncoder::attachIndex(uint pin, bool onRisingEdge, bool pullUp, uint32_t debounceUs) {
  if (index_attached_ || index_slot_count >= kMaxIndexSlots) {
    return -1;
  }

  index_pin_ = pin;
  index_debounce_us_ = debounceUs;
  index_isr_count_ = 0;
  index_processed_count_ = 0;
  index_latched_ = false;
  zero_armed_ = false;

  gpio_init(pin);
  gpio_set_dir(pin, false);
  gpio_set_pulls(pin, pullUp, false);

  const uint32_t ints = save_and_disable_interrupts();
  index_slots[index_slot_count] = { pin, this };
  index_slot_count++;
  restore_interrupts(ints);

  if (!index_dispatcher_added) {
    index_dispatcher_added = true;
    gpio_add_raw_irq_handler(pin, substep_encoder_index_irq_dispatch);
  }
  gpio_set_irq_enabled(pin, onRisingEdge ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL, true);
  irq_set_enabled(IO_IRQ_BANK0, true);

  index_attached_ = true;
  return 0;
}

void SubstepEncoder::detachIndex() {
  if (!index_attached_) {
    return;
  }
  gpio_set_irq_enabled(index_pin_, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);

  const uint32_t ints = save_and_disable_interrupts();
  for (int i = 0; i < index_slot_count; i++) {
    if (index_slots[i].enc == this) {
      index_slots[i] = index_slots[index_slot_count - 1];
      index_slot_count--;
      break;
    }
  }
  restore_interrupts(ints);

  index_attached_ = false;
}

bool SubstepEncoder::indexSeen() {
  maybeRefresh();
  return index_latched_;
}

int64_t SubstepEncoder::lastIndexPosition() {
  maybeRefresh();
  return last_index_position_;
}

void SubstepEncoder::zeroOnNextIndex(int64_t positionAtIndex) {
  zero_target_ = positionAtIndex;
  zero_armed_ = true;
}

bool SubstepEncoder::zeroPending() {
  maybeRefresh();
  return zero_armed_;
}

int64_t SubstepEncoder::lastIndexSpacing() {
  maybeRefresh();
  if (index_latch_count_ < 2) {
    return 0;
  }
  return last_index_position_ - prev_index_position_;
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
