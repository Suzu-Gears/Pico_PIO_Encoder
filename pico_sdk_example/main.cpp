// SubstepEncoder Pico SDK example: print position and speed over USB stdio.

#include <cstdio>

#include "pico/stdlib.h"

#include "SubstepEncoder.h"

int main() {
  stdio_init_all();

  SubstepEncoder encoder;
  if (encoder.begin(2) != 0) {
    while (true) {
      printf("No free PIO block for the encoder\n");
      sleep_ms(1000);
    }
  }

  while (true) {
    SubstepEncoder::Snapshot s = encoder.read();
    printf("position=%lld substeps, speed=%ld substeps/s\n",
           (long long)s.position, (long)s.speed);
    sleep_ms(100);
  }
}
