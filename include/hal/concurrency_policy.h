#ifndef STM32H7_DRIVERS_INCLUDE_HAL_CONCURRENCY_POLICY_
#define STM32H7_DRIVERS_INCLUDE_HAL_CONCURRENCY_POLICY_

#include <conc/concurrency.hpp>

#include <concepts>
#include <utility>

#include "hal/cortex_m_cpu.h"

namespace hal {

// Critical-section policy for Intel's cpp-baremetal-concurrency (used by
// groov's async layer). The library is built freestanding (no std::mutex), so
// critical sections mask interrupts through PRIMASK and restore it on exit,
// which makes them nestable and usable from ISRs.
struct InterruptLockPolicy {
  template <typename = void, std::invocable F, std::predicate... Pred>
    requires(sizeof...(Pred) < 2)
  static auto call_in_critical_section(F&& f, Pred&&... pred)
      -> decltype(std::forward<F>(f)()) {
    while (true) {
      const cortex_m::InterruptLock lock;
      if ((... && pred())) {
        return std::forward<F>(f)();
      }
    }
  }
};

}  // namespace hal

template <>
inline auto conc::injected_policy<> = hal::InterruptLockPolicy{};

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_CONCURRENCY_POLICY_
