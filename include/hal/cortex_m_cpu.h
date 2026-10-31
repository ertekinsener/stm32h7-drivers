#ifndef STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_CPU_
#define STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_CPU_

#include <cstdint>

namespace hal::cortex_m {

// Core special registers and barriers (ARMv7-M): these are CPU
// instructions, not memory-mapped registers, so they are inline asm and not
// part of the groov register layer. The asm statements are compiler barriers
// ("memory").

inline std::uint32_t GetPrimask() noexcept {
  std::uint32_t value;
  asm volatile("mrs %0, primask" : "=r"(value)::"memory");
  return value;
}
inline void SetPrimask(std::uint32_t value) noexcept {
  asm volatile("msr primask, %0" ::"r"(value) : "memory");
}
inline void DisableInterrupts() noexcept { asm volatile("cpsid i" ::: "memory"); }

inline std::uint32_t GetFaultmask() noexcept {
  std::uint32_t value;
  asm volatile("mrs %0, faultmask" : "=r"(value)::"memory");
  return value;
}
inline void SetFaultmask(std::uint32_t value) noexcept {
  asm volatile("msr faultmask, %0" ::"r"(value) : "memory");
}

inline void Dsb() noexcept { asm volatile("dsb 0xF" ::: "memory"); }
inline void Isb() noexcept { asm volatile("isb 0xF" ::: "memory"); }

// Masks interrupts (PRIMASK) and restores the previous mask on exit, so
// locks nest and work in ISRs.
class InterruptLock {
 public:
  InterruptLock() noexcept : primask_(GetPrimask()) { DisableInterrupts(); }
  InterruptLock(const InterruptLock&) = delete;
  InterruptLock& operator=(const InterruptLock&) = delete;
  ~InterruptLock() { SetPrimask(primask_); }

 private:
  std::uint32_t primask_;
};

}  // namespace hal::cortex_m

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_CPU_
