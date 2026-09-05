#pragma once
#include "hardware/sync.h"

namespace substep_encoder {
// Short copy-only critical sections. Disabling local IRQs prevents an ISR
// reader from deadlocking a foreground writer on the same core. SDK striped
// spinlocks are shared, so never call another locking API inside the guard.
class CopyLock {
public:
  CopyLock() : lock_(spin_lock_instance(next_striped_spin_lock_num())) {}
  uint32_t enter() const { return spin_lock_blocking(lock_); }
  void leave(uint32_t saved) const { spin_unlock(lock_, saved); }
  CopyLock(const CopyLock &) = delete;
  CopyLock &operator=(const CopyLock &) = delete;
private:
  spin_lock_t *lock_;
};

template<class T> class Published {
public:
  T load() const {
    const uint32_t saved = lock_.enter();
    const T copy = value_;
    lock_.leave(saved);
    return copy;
  }
  void store(const T &value) {
    const uint32_t saved = lock_.enter();
    value_ = value;
    lock_.leave(saved);
  }
private:
  CopyLock lock_;
  T value_{};
};
} // namespace substep_encoder
