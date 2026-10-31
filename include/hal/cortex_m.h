#ifndef STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_
#define STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_

#include <groov/groov.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/cortex_m_cpu.h"
#include "hal/detail/ct_name.h"
#include "stm32h7/irq.h"
#include "stm32h7/regs/nvic.h"
#include "stm32h7/regs/nvic_ipr.h"
#include "stm32h7/regs/scb.h"

namespace hal::cortex_m {

// The Cortex-M7 core functions the drivers use, in place of CMSIS-Core:
// NVIC, SCB and D-cache maintenance with groov on the generated core maps
// (stm32h7::regs::nvic, nvic_ipr, scb); the special registers are in
// hal/cortex_m_cpu.h. The interrupt is a template argument: groov register
// names are compile-time, and the drivers know their interrupts.
//
// Driver-internal: groov code, for the driver sources that are built with
// -O2 (stream and port instances, hal/*.cc).

// Encodes a preemption priority (subpriority 0) with the current priority
// grouping, like CMSIS NVIC_EncodePriority().
inline std::uint32_t EncodePriority(std::uint32_t preempt) noexcept {
  constexpr std::uint32_t kBits = stm32h7::kNvicPriorityBits;
  const std::uint32_t group = groov::sync_read(
      stm32h7::regs::scb::kScb / groov::path<"aircr", "prigroup">{});
  const std::uint32_t preempt_bits = 7U - group > kBits ? kBits : 7U - group;
  const std::uint32_t sub_bits = group + kBits < 7U ? 0U : group + kBits - 7U;
  return (preempt & ((1U << preempt_bits) - 1U)) << sub_bits;
}

// Sets the preemption priority of `I` (0 = highest): one byte store.
template <stm32h7::Irq I>
void SetPriority(std::uint32_t preempt) noexcept {
  static constexpr auto kIp = detail::Name<"ip", std::to_underlying(I)>();
  groov::sync_write(stm32h7::regs::nvic_ipr::kNvicIpr(
      groov::path<kIp, "pri">{} = static_cast<std::uint8_t>(
          EncodePriority(preempt) << (8U - stm32h7::kNvicPriorityBits))));
}

// Enables `I`: one store to NVIC_ISERn (writing 0 has no effect).
template <stm32h7::Irq I>
void EnableIrq() noexcept {
  constexpr std::size_t kNumber = std::to_underlying(I);
  static constexpr auto kIser = detail::Name<"iser", kNumber / 32U>();
  asm volatile("" ::: "memory");
  groov::sync_write(stm32h7::regs::nvic::kNvic(
      groov::path<kIser, "setena">{} = std::uint32_t{1} << (kNumber % 32U)));
  asm volatile("" ::: "memory");
}

// Pends `I`; its handler runs once the priority allows.
template <stm32h7::Irq I>
void SetPendingIrq() noexcept {
  constexpr std::size_t kNumber = std::to_underlying(I);
  static constexpr auto kIspr = detail::Name<"ispr", kNumber / 32U>();
  groov::sync_write(stm32h7::regs::nvic::kNvic(
      groov::path<kIspr, "setpend">{} = std::uint32_t{1} << (kNumber % 32U)));
}

// SCB_CCR.BFHFNMIGN: with FAULTMASK set, precise bus faults are ignored.
inline void SetBusFaultIgnore(bool ignore) noexcept {
  groov::sync_write(
      stm32h7::regs::scb::kScb(groov::path<"ccr", "bfhfnmign">{} = ignore));
}

// Invalidates the D-cache lines covering [address, address + size), like
// CMSIS SCB_InvalidateDCache_by_Addr(). Data in those lines that was not
// written back is lost.
inline void InvalidateDCache(std::uintptr_t address, std::size_t size) noexcept {
  constexpr std::uintptr_t kLine = 32;
  if (size == 0) {
    return;
  }
  const std::uintptr_t end = address + size;
  Dsb();
  for (std::uintptr_t line = address & ~(kLine - 1U); line < end; line += kLine) {
    groov::sync_write(stm32h7::regs::scb::kScb(
        groov::path<"dcimvac", "address">{} = static_cast<std::uint32_t>(line)));
  }
  Dsb();
  Isb();
}

}  // namespace hal::cortex_m

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_CORTEX_M_
