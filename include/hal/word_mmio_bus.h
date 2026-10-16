#ifndef STM32H7_DRIVERS_INCLUDE_HAL_WORD_MMIO_BUS_
#define STM32H7_DRIVERS_INCLUDE_HAL_WORD_MMIO_BUS_

#include <async/concepts.hpp>
#include <async/just_result_of.hpp>
#include <stdx/ct_string.hpp>

#include <concepts>
#include <cstdint>
#include <limits>

#include "hal/concurrency_policy.h"

namespace hal {

// groov bus for peripherals that only accept full-width register accesses,
// such as DMA and DMAMUX ("registers have to be accessed by words", RM0399).
// groov::mmio_bus narrows a write to a byte/halfword store when the mask
// allows it; this bus never does.
struct WordMmioBus {
  template <stdx::ct_string, std::unsigned_integral auto Mask>
  static auto read(auto address) -> async::sender auto {
    using Word = decltype(Mask);
    return async::just_result_of(
        [=]() -> Word { return *Register<Word>(address); });
  }

  template <stdx::ct_string, auto Mask, decltype(Mask) IdMask,
            decltype(Mask) IdValue>
    requires std::unsigned_integral<decltype(Mask)>
  static auto write(auto address, decltype(Mask) value)
      -> async::sender auto {
    using Word = decltype(Mask);
    return async::just_result_of([=]() -> void {
      volatile Word* const reg = Register<Word>(address);
      if constexpr ((Mask | IdMask) == std::numeric_limits<Word>::max()) {
        *reg = static_cast<Word>(value | IdValue);
      } else {
        *reg = static_cast<Word>((*reg & ~(Mask | IdMask)) | (value & Mask) |
                                 IdValue);
      }
    });
  }

 private:
  template <std::unsigned_integral Word>
  static auto Register(std::uintptr_t address) -> volatile Word* {
    return reinterpret_cast<volatile Word*>(address);
  }
};

}  // namespace hal

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_WORD_MMIO_BUS_
