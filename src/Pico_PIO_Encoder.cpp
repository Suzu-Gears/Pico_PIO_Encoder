/*
  Pico_PIO_Encoder - high resolution quadrature encoder for RP2040/RP2350

  PIO glue: allocation of PIO blocks / state machines, program setup and
  raw sample reading. The estimation logic lives in
  substep_encoder_estimator.h.

  Based on PicoEncoder by Paulo Marques, Pedro Pereira, Paulo Costa
  (BSD 2-clause). Distributed under the BSD 2-clause license, see LICENSE.
*/

#include "Pico_PIO_Encoder.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/mutex.h"

#include "substep_encoder.pio.h"
#include "pair_encoder.pio.h"
#include "substep_encoder_pins.h"

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

// Lifecycle operations are foreground-only. This mutex serializes our
// resource bookkeeping across cores without nesting hardware spinlocks.
auto_init_mutex(resource_mutex);
struct ResourceGuard {
  ResourceGuard() { mutex_enter_blocking(&resource_mutex); }
  ~ResourceGuard() { mutex_exit(&resource_mutex); }
};
using Mode = Pico_PIO_Encoder::Mode;
using Pull = Pico_PIO_Encoder::Pull;

bool validPull(Pull pull) {
  return pull == Pull::None || pull == Pull::Up || pull == Pull::Down;
}

void configurePull(uint pin, Pull pull) {
  gpio_set_pulls(pin, pull == Pull::Up, pull == Pull::Down);
}
struct PioSlot {
  uint used_mask = 0, offset = 0, previous_base = 0;
  Mode mode = Mode::None;
};
PioSlot pio_slots[NUM_PIOS];

const pio_program *program(Mode mode) {
  return mode == Mode::Consecutive ? &substep_encoder_program : &pair_encoder_program;
}
uint gpioBase(PIO p) {
#if PICO_PIO_VERSION > 0
  return pio_get_gpio_base(p);
#else
  (void)p; return 0;
#endif
}
void setGpioBase(PIO p, uint base) {
#if PICO_PIO_VERSION > 0
  pio_set_gpio_base(p, base);
#else
  (void)p; (void)base;
#endif
}
uint64_t gpioLevels() {
#if NUM_BANK0_GPIOS > 32
  return gpio_get_all64();
#else
  return gpio_get_all();
#endif
}

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

int allocate_sm(PIO forced, PIO *out_pio, uint *out_sm, Mode mode, uint a, uint b) {
  ResourceGuard guard;
  const uint width = mode == Mode::Consecutive ? 1 : 2;
  const uint mask = (1u << width) - 1;
  // Fill holes in existing blocks first, then claim a completely free one.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < NUM_PIOS; ++i) {
      PIO pio = pio_instance(i);
      auto &slot = pio_slots[i];
      if (forced && forced != pio) continue;
      if ((pass == 0) != (slot.used_mask != 0)) continue;
      if (slot.used_mask && (slot.mode != mode || !substep_encoder::pinsInWindow(a,b,gpioBase(pio)))) continue;
      if (slot.used_mask == (1u << NUM_PIO_STATE_MACHINES) - 1) continue;
      if (!slot.used_mask) {
        bool free = true;
        for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
          if (pio_sm_is_claimed(pio, sm)) free = false;
        if (!free) continue;
        if (!pio_can_add_program(pio, program(mode))) continue;
        slot.previous_base = gpioBase(pio);
        const uint base = substep_encoder::pinsInWindow(a,b,slot.previous_base) ?
          slot.previous_base : (substep_encoder::pinsInWindow(a,b,0) ? 0 : 16);
        setGpioBase(pio,base);
        pio_claim_sm_mask(pio, (1u << NUM_PIO_STATE_MACHINES) - 1);
        slot.offset = pio_add_program(pio, program(mode));
        slot.mode = mode;
      }
      for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm += width) {
        if (!(slot.used_mask & (mask << sm))) {
          slot.used_mask |= mask << sm;
          *out_pio = pio;
          *out_sm = sm;
          return 0;
        }
      }
    }
  }
  return -1;
}

void release_sm(PIO pio, uint sm) {
  ResourceGuard guard;
  for (int i = 0; i < NUM_PIOS; ++i) {
    if (pio_instance(i) != pio) continue;
    auto &slot = pio_slots[i];
    const uint width = slot.mode == Mode::Consecutive ? 1 : 2;
    const uint mask = ((1u << width) - 1) << sm;
    pio_set_sm_mask_enabled(pio, mask, false);
    for(uint n=sm; n<sm+width; ++n) {pio_sm_clear_fifos(pio,n);pio_sm_restart(pio,n);}
    slot.used_mask &= ~mask;
    if (!slot.used_mask) {
      pio_remove_program(pio, program(slot.mode), slot.offset);
      setGpioBase(pio,slot.previous_base);
      slot.mode = Mode::None;
      for (uint n = 0; n < NUM_PIO_STATE_MACHINES; ++n) pio_sm_unclaim(pio, n);
    }
    return;
  }
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

  const uint32_t pin_state = (gpioLevels() >> pin_A) & 3;
  pio_sm_exec(pio, sm, pio_encode_set(pio_x, 0));

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

uint32_t pair_program_init(PIO pio, uint sm, uint a, uint b) {
  pio_gpio_init(pio,a);pio_gpio_init(pio,b);
  pio_sm_set_consecutive_pindirs(pio,sm,a,1,false);
  pio_sm_set_consecutive_pindirs(pio,sm,b,1,false);
  const uint32_t ints=save_and_disable_interrupts();
  const uint64_t pins=gpioLevels();
  const uint av=(pins>>a)&1, bv=(pins>>b)&1;
  static const uint phases[4]={0,3,1,2};
  for(uint phase=0;phase<2;phase++) {
    auto c=pair_encoder_program_get_default_config(0);
    sm_config_set_in_pins(&c,phase?b:a);
    sm_config_set_jmp_pin(&c,phase?a:b);
    sm_config_set_in_shift(&c,false,true,32);
    sm_config_set_out_shift(&c,true,false,32);
    sm_config_set_mov_status(&c,STATUS_RX_LESSTHAN,1);
    pio_sm_init(pio,sm+phase,2,&c);
    pio_sm_exec(pio,sm+phase,pio_encode_set(pio_x,0));
    pio_sm_exec(pio,sm+phase,pio_encode_set(pio_y,phase?bv:av));
  }
  pio_enable_sm_mask_in_sync(pio,3u<<sm);
  restore_interrupts(ints);
  return phases[av|(bv<<1)]-(av-bv);
}

struct RawSample {uint32_t step,loops;uint64_t acquired;bool forward;};
bool fifoWord(PIO pio,uint sm,uint32_t &value,uint32_t deadline) {
  while(pio_sm_is_rx_fifo_empty(pio,sm))
    if(int32_t(time_us_32()-deadline)>=0)return false;
  value=pio_sm_get(pio,sm);return true;
}
// Caller masks interrupts across the whole transaction, including FIFO level.
bool readRaw(PIO pio,uint sm,RawSample &r) {
  uint32_t x=0;
  const uint32_t deadline=time_us_32()+20;
  const uint pairs=(pio_sm_get_rx_fifo_level(pio,sm)>>1)+1;
  for(uint n=0;n<pairs;n++)
    if(!fifoWord(pio,sm,x,deadline)||!fifoWord(pio,sm,r.step,deadline))return false;
  r.acquired=time_us_64();r.forward=(x&0x80000000u)!=0;
  r.loops=r.forward?0u-x:0x80000000u-x;return true;
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
  Pico_PIO_Encoder *enc;
};

constexpr int kMaxIndexSlots = 12;
IndexSlot index_slots[kMaxIndexSlots];
int index_slot_count = 0;
uint32_t index_pin_mask = 0;
int index_core = -1;

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
// Pico_PIO_Encoder
// ---------------------------------------------------------------------------

int Pico_PIO_Encoder::beginConsecutive(uint a,uint b,Pull pull) {
  return beginConsecutive(a,b,nullptr,pull);
}
int Pico_PIO_Encoder::beginConsecutive(uint a,uint b,PIO pio,Pull pull) {
  return beginPins(a,b,Mode::Consecutive,pio,pull);
}
int Pico_PIO_Encoder::beginNonConsecutive(uint a,uint b,Pull pull) {
  return beginNonConsecutive(a,b,nullptr,pull);
}
int Pico_PIO_Encoder::beginNonConsecutive(uint a,uint b,PIO pio,Pull pull) {
  return beginPins(a,b,Mode::NonConsecutive,pio,pull);
}
int Pico_PIO_Encoder::beginPins(uint a,uint b,Mode mode,PIO pio,Pull pull) {
  if(pio_ || !validPull(pull) || !substep_encoder::validPins(a,b,NUM_BANK0_GPIOS,mode==Mode::Consecutive))return -2;
  if(pio) {
    bool found=false;
    for(int i=0;i<NUM_PIOS;i++)if(pio_instance(i)==pio)found=true;
    if(!found)return -2;
  }
  if(allocate_sm(pio,&pio_,&sm_,mode,a,b)<0)return -1;
  owner_core_=int(get_core_num());mode_=mode;pin_a_=a;pin_b_=b;
  invert_raw_=mode==Mode::Consecutive && a>b;
  pio_fault_=read_failed_=false;read_failures_=read_retries_=0;
  configurePull(a,pull);configurePull(b,pull);
  if(mode==Mode::Consecutive)program_init(pio_,sm_,a<b?a:b);
  else pair_offset_=pair_program_init(pio_,sm_,a,b);
  system_clock_hz_=clock_get_hz(clk_sys);
  Sample s;uint64_t acquired;
  if(!readSample(&s,&acquired)){end();return -3;}
  estimator_.reset(s);estimator_.resetCalibration();
  last_refresh_us_=s.sample_us;last_refresh_us64_=acquired;
  update_late_=false;publish();return 0;
}

bool Pico_PIO_Encoder::readSample(Sample *s,uint64_t *timestamp64) {
  if(pio_fault_)return false;
  RawSample a{},b{},after{};bool ok=false;
  const uint32_t ints=save_and_disable_interrupts();
  for(uint attempt=0;attempt<(mode_==Mode::Consecutive?1u:8u);attempt++) {
    if(!readRaw(pio_,sm_,a)){pio_fault_=true;break;}
    if(mode_==Mode::Consecutive){ok=true;break;}
    if(!readRaw(pio_,sm_+1,b)||!readRaw(pio_,sm_,after)){pio_fault_=true;break;}
    if(a.step==after.step && a.forward==after.forward && after.loops>=a.loops){ok=true;break;}
    if(read_retries_!=UINT32_MAX)++read_retries_;
  }
  restore_interrupts(ints);
  if(!ok)return false;
  const uint32_t clocks=mode_==Mode::Consecutive?13:17;
  const uint32_t edgeA=uint32_t(a.acquired)-uint32_t(uint64_t(a.loops)*clocks*1000000/system_clock_hz_);
  uint64_t acquired=a.acquired;
  if(mode_==Mode::Consecutive) {
    s->step=invert_raw_?0u-a.step:a.step;
    s->forward=a.forward!=invert_raw_;s->transition_us=edgeA;
  } else {
    const uint32_t edgeB=uint32_t(b.acquired)-uint32_t(uint64_t(b.loops)*17000000/system_clock_hz_);
    const bool lastA=int32_t(edgeA-edgeB)>0;
    s->step=a.step-b.step+pair_offset_;s->forward=lastA?a.forward:!b.forward;
    s->transition_us=lastA?edgeA:edgeB;acquired=b.acquired;
  }
  s->sample_us=uint32_t(acquired);if(timestamp64)*timestamp64=acquired;
  return true;
}
void Pico_PIO_Encoder::refresh() {
  if (pio_ == nullptr || owner_core_ != int(get_core_num())) return;
  Sample s;
  uint64_t now;
  if(!readSample(&s, &now)) {
    read_failed_=true;if(read_failures_!=UINT32_MAX)++read_failures_;
    publish();return;
  }
  read_failed_=false;
  estimator_.update(s);
  if (auto_calibrate_) {
    estimator_.updateCalibration(s);
  }
  update_late_ = max_sample_age_us_ && now - last_refresh_us64_ > max_sample_age_us_;
  last_refresh_us64_ = now;
  last_refresh_us_ = s.sample_us;

  if (index_attached_) {
    processIndexEvents();
  }
  publish();
}

// ---------------------------------------------------------------------------
// Index input
// ---------------------------------------------------------------------------

// The foreground FIFO transaction also disables interrupts, including the
// FIFO-level read. The ISR can therefore capture without splitting a pair.
void Pico_PIO_Encoder::onIndexIrq() {
  Sample s;
  if(!readSample(&s)) {
    read_failed_=true;if(read_failures_!=UINT32_MAX)++read_failures_;
    publish();return;
  }
  index_capture_.record(s.step, s.sample_us, index_debounce_us_);
}

// Reconstruct coordinates from captured COUNTS, never from current velocity.
// Most recent two events and the first armed event survive delayed polling.
void Pico_PIO_Encoder::processIndexEvents() {
  const uint32_t ints = save_and_disable_interrupts();
  if (index_capture_.count() == index_processed_count_) {
    restore_interrupts(ints);
    return;
  }
  index_processed_count_ = index_capture_.count();
  if (index_capture_.zeroCaptured()) {
    const int64_t at_zero = estimator_.measuredPositionAtStep(index_capture_.zeroStep());
    bool saturated = false;
    estimator_.shiftPositionSteps(substep_encoder::saturatedSubtract(index_capture_.target(), at_zero, saturated));
    index_capture_.completeZero();
  }
  index_latched_ = index_capture_.seen();
  index_have_pair_ = index_capture_.havePair();
  if (index_latched_) {
    last_index_position_ = estimator_.measuredPositionAtStep(index_capture_.latestStep());
    if (index_have_pair_) {
      bool saturated = false;
      prev_index_position_ = substep_encoder::saturatedAdd(last_index_position_, substep_encoder::counterDelta(
          index_capture_.previousStep(), index_capture_.latestStep()), saturated);
    }
  }
  restore_interrupts(ints);
}

int Pico_PIO_Encoder::attachIndex(uint pin, bool onRisingEdge, Pull pull, uint32_t debounceUs) {
  if (pio_ == nullptr || owner_core_ != int(get_core_num()) || !validPull(pull) || pin >= NUM_BANK0_GPIOS || pin >= 32 || pin==pin_a_ || pin==pin_b_) return -2;
  ResourceGuard guard;
  const uint32_t ints = save_and_disable_interrupts();
  if (index_attached_ || index_slot_count >= kMaxIndexSlots ||
      (index_pin_mask & (1u << pin)) ||
      (index_core >= 0 && index_core != int(get_core_num()))) {
    restore_interrupts(ints);
    return -1;
  }
  index_pin_ = pin;
  index_debounce_us_ = debounceUs;
  index_capture_.reset();
  index_processed_count_ = 0;
  index_latched_ = index_have_pair_ = false;
  last_index_position_ = prev_index_position_ = 0;
  gpio_init(pin);
  gpio_set_dir(pin, false);
  configurePull(pin, pull);
  // Register the complete pin mask; otherwise the SDK's default callback
  // may consume events on the second and later index pins.
  if (index_pin_mask) gpio_remove_raw_irq_handler_masked(index_pin_mask, substep_encoder_index_irq_dispatch);
  index_slots[index_slot_count++] = {pin, this};
  index_pin_mask |= 1u << pin;
  index_core = int(get_core_num());
  gpio_add_raw_irq_handler_masked(index_pin_mask, substep_encoder_index_irq_dispatch);
  gpio_acknowledge_irq(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
  gpio_set_irq_enabled(pin, onRisingEdge ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL, true);
  irq_set_enabled(IO_IRQ_BANK0, true);
  index_attached_ = true;
  restore_interrupts(ints);
  return 0;
}

void Pico_PIO_Encoder::detachIndex() {
  if (!index_attached_ || owner_core_ != int(get_core_num())) return;
  ResourceGuard guard;
  const uint32_t ints = save_and_disable_interrupts();
  gpio_set_irq_enabled(index_pin_, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
  gpio_remove_raw_irq_handler_masked(index_pin_mask, substep_encoder_index_irq_dispatch);
  index_pin_mask &= ~(1u << index_pin_);
  for (int i = 0; i < index_slot_count; i++) {
    if (index_slots[i].enc == this) {
      index_slots[i] = index_slots[--index_slot_count];
      break;
    }
  }
  if (index_pin_mask) gpio_add_raw_irq_handler_masked(index_pin_mask, substep_encoder_index_irq_dispatch);
  else index_core = -1;
  index_capture_.reset();
  index_processed_count_ = 0;
  index_attached_ = false;
  index_latched_ = index_have_pair_ = false;
  restore_interrupts(ints);
}

bool Pico_PIO_Encoder::indexSeen() {
  maybeRefresh();
  return index_latched_;
}

uint32_t Pico_PIO_Encoder::indexCount() const {
  const uint32_t ints = save_and_disable_interrupts();
  const uint32_t count = index_capture_.count();
  restore_interrupts(ints);
  return count;
}

int64_t Pico_PIO_Encoder::lastIndexPosition() {
  maybeRefresh();
  return last_index_position_;
}

void Pico_PIO_Encoder::zeroOnNextIndex(int64_t positionAtIndex) {
  if (!index_attached_ || owner_core_ != int(get_core_num())) return;
  const uint32_t ints = save_and_disable_interrupts();
  index_capture_.arm(positionAtIndex);
  restore_interrupts(ints);
}

bool Pico_PIO_Encoder::zeroPending() {
  maybeRefresh();
  const uint32_t ints = save_and_disable_interrupts();
  const bool pending = index_capture_.armed();
  restore_interrupts(ints);
  return pending;
}

int64_t Pico_PIO_Encoder::lastIndexSpacing() {
  maybeRefresh();
  bool saturated = false;
  return index_have_pair_ ? substep_encoder::saturatedSubtract(last_index_position_, prev_index_position_, saturated) : 0;
}
void Pico_PIO_Encoder::maybeRefresh() {
  if ((uint32_t)(time_us_32() - last_refresh_us_) >= min_refresh_interval_us_) {
    refresh();
  }
}

int64_t Pico_PIO_Encoder::positionSubsteps() {
  maybeRefresh();
  return estimator_.position();
}

int32_t Pico_PIO_Encoder::speedSubsteps() {
  maybeRefresh();
  return estimator_.speed();
}

// Measured counts, independent of interpolation and the velocity schedule.
int64_t Pico_PIO_Encoder::position() {
  maybeRefresh();
  return estimator_.measuredPosition();
}

float Pico_PIO_Encoder::speed() {
  return (float)speedSubsteps() / 64.0f;
}

bool Pico_PIO_Encoder::stopped() {
  maybeRefresh();
  return estimator_.stopped();
}

Pico_PIO_Encoder::~Pico_PIO_Encoder() {
  // Destroy on the owner core; silently leaving an IRQ pointer is unsafe.
  hard_assert(end());
}

bool Pico_PIO_Encoder::end() {
  if (pio_) {
    if (owner_core_ != int(get_core_num())) return false;
    detachIndex();
    release_sm(pio_, sm_);
    pio_ = nullptr;
    owner_core_ = -1;
  }
  estimator_.reset({0, 0, 0, false});
  index_capture_.reset();
  index_processed_count_ = 0;
  index_latched_ = index_have_pair_ = false;
  last_index_position_ = prev_index_position_ = 0;
  last_refresh_us_ = 0;
  last_refresh_us64_ = 0;
  update_late_ = false;
  mode_=Mode::None;pin_a_=pin_b_=0;
  read_failed_=pio_fault_=false;read_failures_=read_retries_=0;
  publish();
  return true;
}

void Pico_PIO_Encoder::publish() {
  uint32_t status = 0;
  if (pio_) {
    status = Initialized | (estimator_.velocityReady() ? VelocityValid : VelocityWarmingUp);
    if (update_late_) status |= UpdateLate;
    if (estimator_.speedSaturated()) status |= SpeedSaturated;
    if (estimator_.positionSaturated()) status |= PositionSaturated;
    if(read_failed_)status|=ReadFailed;
    if(pio_fault_)status|=PioFault;
  }
  const Snapshot s{estimator_.measuredPosition(), float(estimator_.speed()) / 64.0f,
    last_refresh_us_, estimator_.speedTimestampUs(), estimator_.position(), estimator_.stopped(),
    status, ++sequence_, last_refresh_us64_, max_sample_age_us_,
    mode_,uint8_t(pin_a_),uint8_t(pin_b_),uint8_t(pio_?(mode_==Mode::Consecutive?1:2):0),read_failures_,read_retries_};
  published_.store(s);
}

void Pico_PIO_Encoder::updateFreshness(Snapshot &s, uint64_t now_us) {
  s.status &= ~Stale;
  if (s.has(Initialized) && s.max_age_us && now_us - s.timestamp_us64 > s.max_age_us)
    s.status |= Stale;
}

Pico_PIO_Encoder::Snapshot Pico_PIO_Encoder::latest() const {
  Snapshot s = published_.load();
  updateFreshness(s, time_us_64());
  return s;
}

Pico_PIO_Encoder::Snapshot Pico_PIO_Encoder::read() {
  maybeRefresh();
  return latest();
}

void Pico_PIO_Encoder::resetPosition(int64_t to) {
  if (!pio_ || owner_core_ != int(get_core_num())) return;
  refresh();
  if(read_failed_ || pio_fault_)return;
  bool saturated = false;
  const int64_t offset = substep_encoder::saturatedSubtract(to, estimator_.measuredPosition(), saturated);
  estimator_.setPositionSteps(to);
  last_index_position_ = substep_encoder::saturatedAdd(last_index_position_, offset, saturated);
  prev_index_position_ = substep_encoder::saturatedAdd(prev_index_position_, offset, saturated);
  if (index_attached_) processIndexEvents();
  publish();
}
