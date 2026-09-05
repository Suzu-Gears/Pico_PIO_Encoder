// Hardware-independent validation used by both named entry points.
#pragma once
#include <cstdint>
namespace substep_encoder {
inline bool pinsInWindow(uint32_t a, uint32_t b, uint32_t base) {
  return a >= base && b >= base && a - base < 32 && b - base < 32;
}
inline bool validPins(uint32_t a, uint32_t b, uint32_t gpioCount, bool consecutive) {
  if(a >= gpioCount || b >= gpioCount || a == b) return false;
  if(consecutive && (a > b ? a-b : b-a) != 1) return false;
  return pinsInWindow(a,b,0) || (gpioCount > 32 && pinsInWindow(a,b,16));
}
}
