#ifndef STM32H7_DRIVERS_INCLUDE_EXTI_EXTI_
#define STM32H7_DRIVERS_INCLUDE_EXTI_EXTI_

#include <cstdint>

#include "gpio/gpio.h"

namespace exti {

// EXTI driver (RM0399, EXTI): lines 0..31 of register set 1 on the core it
// runs on (C1 registers on the CM7, C2 on the CM4, chosen by CORE_CM4), and
// GPIO pin interrupts on lines 0..15 (SYSCFG_EXTICRn selects the port).
//
// Like gpio::Pin, the class templates are defined with groov in
// exti/detail/exti_impl.h and instantiated by the application in a source
// built with -O2 (with mcu-cmake: the INSTANCES of stm32h7_drivers_add_exti):
//
//   template class exti::PinInterrupt<board::kButton>;
//
// The exti library defines EXTI0_IRQHandler .. EXTI15_10_IRQHandler: they
// clear the pending lines 0..15 and call each line's callback, in interrupt
// context. Lines 5..9 and 10..15 share one NVIC interrupt, so their
// interrupts have one priority (the last one configured).

enum class Edge : std::uint8_t {
  kRising = 0b01,
  kFalling = 0b10,
  kBoth = 0b11,
};

// Called from the EXTI interrupt of a line, with `context`.
struct Callback {
  void (*function)(void* context) noexcept = nullptr;
  void* context = nullptr;

  void operator()() const noexcept {
    if (function != nullptr) {
      function(context);
    }
  }
};

// One EXTI line. Trigger selection and pending flags exist for the
// configurable lines 0..21; the masks for 0..31.
template <std::uint8_t N>
class Line {
  static_assert(N < 32, "register set 1: lines 0..31");

 public:
  static void SetTrigger(Edge edge) noexcept;     // RTSR1/FTSR1
  static void DisableTrigger() noexcept;
  static void EnableInterrupt() noexcept;         // CnIMR1
  static void DisableInterrupt() noexcept;
  static bool IsInterruptEnabled() noexcept;
  static void EnableEvent() noexcept;             // CnEMR1
  static void DisableEvent() noexcept;
  static bool IsPending() noexcept;               // CnPR1
  static void ClearPending() noexcept;            // Write 1: one store.
  static void GenerateSoftwareInterrupt() noexcept;  // SWIER1
};

// The interrupt of a GPIO pin (EXTI line = pin number). Configure the pin as
// an input first (gpio::Pin::ConfigureInput()).
template <gpio::PinId P>
class PinInterrupt {
  static_assert(P.number < 16, "GPIO pins are 0..15");

 public:
  // Routes the line to the pin's port (SYSCFG, clock enabled), sets the
  // trigger and the callback, clears a stale pending flag, unmasks the line
  // and enables its NVIC interrupt at `irq_priority`.
  static void Configure(Edge edge, Callback callback,
                        std::uint32_t irq_priority) noexcept;
  static void Enable() noexcept;   // Unmask the line.
  static void Disable() noexcept;  // Mask the line.
  // Set while the line is masked; takes effect at the next interrupt.
  static void SetCallback(Callback callback) noexcept;
};

namespace detail {

// The callbacks of lines 0..15, called by the exti library's handlers.
void SetCallback(std::uint8_t line, Callback callback) noexcept;

}  // namespace detail

}  // namespace exti

#endif  // STM32H7_DRIVERS_INCLUDE_EXTI_EXTI_
