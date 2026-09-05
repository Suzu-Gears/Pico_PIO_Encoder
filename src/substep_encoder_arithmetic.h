#pragma once
#include <cstdint>
#include <climits>

namespace substep_encoder {
inline int64_t saturatedAdd(int64_t a, int64_t b, bool &saturated) {
  if (b > 0 && a > INT64_MAX - b) { saturated = true; return INT64_MAX; }
  if (b < 0 && a < INT64_MIN - b) { saturated = true; return INT64_MIN; }
  return a + b;
}
inline int64_t saturatedSubtract(int64_t a, int64_t b, bool &saturated) {
  if (b > 0 && a < INT64_MIN + b) { saturated = true; return INT64_MIN; }
  if (b < 0 && a > INT64_MAX + b) { saturated = true; return INT64_MAX; }
  return a - b;
}
inline int64_t saturatedSubsteps(int64_t steps, bool &saturated) {
  if (steps > INT64_MAX / 64) { saturated = true; return INT64_MAX; }
  if (steps < INT64_MIN / 64) { saturated = true; return INT64_MIN; }
  return steps * 64;
}
} // namespace substep_encoder
