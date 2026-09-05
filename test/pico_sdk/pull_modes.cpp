// On-device pull selection tests. Leave GPIO2/3/6/7/8 externally unconnected.
// USB commands: U = run tests, S = stop, ? = identify firmware.
#include <cstdio>
#include <initializer_list>
#include "pico/stdlib.h"
#include "Pico_PIO_Encoder.h"

using Encoder = Pico_PIO_Encoder;
using Pull = Encoder::Pull;
static unsigned checks, failures;

static void safeStop() {
#if PICO_PIO_ENCODER_NIDEC_FIXTURE
  // Local Nidec fixture: HIGH PWM15 = zero output, LOW BRAKE14 = brake on.
  gpio_put(15, true);
  gpio_set_function(15, GPIO_FUNC_SIO);
  gpio_set_dir(15, true);
  gpio_put(14, false);
  gpio_set_function(14, GPIO_FUNC_SIO);
  gpio_set_dir(14, true);
#endif
}

static void check(bool ok, const char *message) {
  ++checks;
  if (!ok) { ++failures; printf("FAIL %s\n", message); }
}

static void checkPull(uint pin, Pull pull) {
  check(gpio_is_pulled_up(pin) == (pull == Pull::Up), "pull-up register");
  check(gpio_is_pulled_down(pin) == (pull == Pull::Down), "pull-down register");
  sleep_us(100);
  if (pull != Pull::None) check(gpio_get(pin) == (pull == Pull::Up), "biased input level");
}

static bool resourcesFree() {
  for (uint p = 0; p < NUM_PIOS; ++p)
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
      if (pio_sm_is_claimed(pio_get_instance(p), sm)) return false;
  return true;
}

static int begin(Encoder &e, bool separated, bool explicitPio, Pull pull, bool useDefault = false) {
  if (separated) {
    if (useDefault) return explicitPio ? e.beginNonConsecutive(2, 6, pio0) : e.beginNonConsecutive(2, 6);
    return explicitPio ? e.beginNonConsecutive(2, 6, pio0, pull) : e.beginNonConsecutive(2, 6, pull);
  }
  if (useDefault) return explicitPio ? e.beginConsecutive(2, 3, pio0) : e.beginConsecutive(2, 3);
  return explicitPio ? e.beginConsecutive(2, 3, pio0, pull) : e.beginConsecutive(2, 3, pull);
}

static void runTests() {
  safeStop(); checks = failures = 0;
  for (uint pin : {2u, 3u, 6u, 7u, 8u}) { gpio_init(pin); gpio_set_dir(pin, false); }
  check(resourcesFree(), "initial PIO availability");
  const Pull pulls[] = {Pull::Up, Pull::Down, Pull::None};
  unsigned phaseCases = 0, indexCases = 0;
  for (bool separated : {false, true}) {
    const uint b = separated ? 6 : 3;
    for (bool explicitPio : {false, true}) {
      Encoder e;
      for (Pull previous : pulls) for (Pull requested : pulls) {
        check(begin(e, separated, explicitPio, previous) == 0, "previous pull begin");
        check(e.end(), "end before changing pull");
        const int result = begin(e, separated, explicitPio, requested);
        check(result == 0, "requested pull begin");
        if (result == 0) {
          checkPull(2, requested); checkPull(b, requested);
          check(e.read().positionValid(), "PIO acquisition with selected pull");
        }
        check(e.end() && resourcesFree(), "release after pull change");
        ++phaseCases;
      }
      check(begin(e, separated, explicitPio, Pull::Down) == 0, "prepare default test");
      check(e.end(), "release default preparation");
      const int result = begin(e, separated, explicitPio, Pull::Up, true);
      check(result == 0, "default begin");
      if (result == 0) { checkPull(2, Pull::Up); checkPull(b, Pull::Up); }
      check(e.end() && resourcesFree(), "default release");
      ++phaseCases;

      const auto invalid = static_cast<Pull>(255);
      gpio_set_pulls(2, false, true); gpio_set_pulls(b, true, false);
      check(begin(e, separated, explicitPio, invalid) == -2, "reject invalid phase pull");
      check(!e.initialized() && resourcesFree(), "invalid pull leaves resources free");
      checkPull(2, Pull::Down); checkPull(b, Pull::Up);
    }

    Encoder e;
    if (begin(e, separated, false, Pull::None) != 0) {
      check(false, "index test encoder begin");
    } else {
      for (Pull previous : pulls) for (Pull requested : pulls) {
        check(e.attachIndex(8, true, previous) == 0, "index previous attach");
        e.detachIndex();
        const int result = e.attachIndex(8, false, requested, 1000);
        check(result == 0, "index requested attach");
        if (result == 0) checkPull(8, requested);
        checkPull(2, Pull::None); checkPull(b, Pull::None);
        e.detachIndex(); ++indexCases;
      }
      check(e.attachIndex(8, true, Pull::Down) == 0, "prepare index default");
      e.detachIndex();
      check(e.attachIndex(8) == 0, "default index attach");
      checkPull(8, Pull::Up);
      e.detachIndex(); ++indexCases;
      check(e.attachIndex(8, true, static_cast<Pull>(255)) == -2, "reject invalid index pull");
      checkPull(8, Pull::Up);
      check(e.attachIndex(8, true, Pull::Down) == 0, "valid index after rejection");
      checkPull(8, Pull::Down);
      check(e.attachIndex(8, true, Pull::None) == -1, "duplicate index rejects before changing pull");
      checkPull(8, Pull::Down);
    }
    check(e.end() && resourcesFree(), "index resources released");
  }

  {
    Encoder a, b;
    check(a.beginConsecutive(2, 3, pio0, Pull::Down) == 0, "first encoder down");
    check(b.beginConsecutive(6, 7, pio0, Pull::Up) == 0, "second encoder up");
    checkPull(2, Pull::Down); checkPull(3, Pull::Down);
    checkPull(6, Pull::Up); checkPull(7, Pull::Up);
    check(a.beginConsecutive(2, 3, Pull::Up) == -2, "repeated begin rejected");
    checkPull(2, Pull::Down); checkPull(3, Pull::Down);
    check(a.end() && b.end(), "independent encoder release");
  }
  for (uint pin : {2u, 3u, 6u, 7u, 8u}) gpio_disable_pulls(pin);
  check(resourcesFree(), "final PIO availability");
  safeStop();
  printf("RESULT pulls phase_cases=%u index_cases=%u checks=%u failures=%u pio_free=%d\n",
         phaseCases, indexCases, checks, failures, resourcesFree());
}

int main() {
  safeStop();
  stdio_init_all();
  while (true) {
    const int c = getchar_timeout_us(10000);
    if (c == 'U') runTests();
    else if (c == 'S') {
      safeStop();
#if PICO_PIO_ENCODER_NIDEC_FIXTURE
      printf("STOP PWM15=HIGH BRAKE14=LOW\n");
#else
      printf("STOP\n");
#endif
    }
    else if (c == '?') printf("Pico_PIO_Encoder Pull validation; U=test GPIO2/3/6/7/8\n");
  }
}
