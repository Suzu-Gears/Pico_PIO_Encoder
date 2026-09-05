// Pico_PIO_Encoder Pico SDK example: print position and speed over USB stdio.

#include <cstdio>

#include "pico/stdlib.h"

#include "Pico_PIO_Encoder.h"

int main() {
  stdio_init_all();

  Pico_PIO_Encoder encoder;
  const int result = encoder.beginConsecutive(2, 3);
  if (result != 0) {
    while (true) {
      printf("%s\n", Pico_PIO_Encoder::beginErrorMessage(result));
      sleep_ms(1000);
    }
  }

  while (true) {
    Pico_PIO_Encoder::Snapshot s = encoder.read();
    printf("position=%lld steps, speed=%.2f steps/s\n",
           (long long)s.position, (double)s.speed);
    sleep_ms(100);
  }
}
