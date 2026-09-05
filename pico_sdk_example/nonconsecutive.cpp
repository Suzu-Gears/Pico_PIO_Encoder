// Pico SDK example: A on GPIO2, B on GPIO10; two SMs per encoder.
#include <cstdio>
#include "pico/stdlib.h"
#include "Pico_PIO_Encoder.h"

int main() {
  stdio_init_all();
  Pico_PIO_Encoder encoder;
  const int result = encoder.beginNonConsecutive(2, 10);
  if (result != 0) {
    while (true) {
      printf("%s\n", Pico_PIO_Encoder::beginErrorMessage(result));
      sleep_ms(1000);
    }
  }
  while (true) {
    const auto sample = encoder.read();
    if (sample.positionValid()) {
      printf("position=%lld steps, speed=%.2f steps/s, speed_valid=%d\n",
             (long long)sample.position, (double)sample.speed,
             sample.speedValid());
    }
    sleep_ms(100);
  }
}
