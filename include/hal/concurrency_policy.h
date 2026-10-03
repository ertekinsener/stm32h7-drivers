#ifndef APP_INCLUDE_HAL_CONCURRENCY_POLICY_
#define APP_INCLUDE_HAL_CONCURRENCY_POLICY_

#include <conc/concurrency.hpp>

#include <concepts>
#include <cstdint>
#include <utility>

#include "stm32h7xx.h"

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
      const PrimaskGuard guard;
      if ((... && pred())) {
        return std::forward<F>(f)();
      }
    }
  }

 private:
  class PrimaskGuard {
   public:
    PrimaskGuard() noexcept : primask_(__get_PRIMASK()) { __disable_irq(); }
    PrimaskGuard(const PrimaskGuard&) = delete;
    PrimaskGuard& operator=(const PrimaskGuard&) = delete;
    ~PrimaskGuard() { __set_PRIMASK(primask_); }

   private:
    std::uint32_t primask_;
  };
};

}  // namespace hal

template <>
inline auto conc::injected_policy<> = hal::InterruptLockPolicy{};

#endif  // APP_INCLUDE_HAL_CONCURRENCY_POLICY_
