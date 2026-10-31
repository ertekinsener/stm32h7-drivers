#ifndef EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PINS_
#define EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PINS_

// Pins of the NUCLEO-H755ZI-Q used by the example. Their driver instances
// are compiled in board/pins.cc and board/interrupts.cc (INSTANCES of
// stm32h7_drivers_add_gpio() and stm32h7_drivers_add_exti()).

#include "exti/exti.h"
#include "gpio/gpio.h"

namespace board {

inline constexpr gpio::PinId kLd1{gpio::Port::kB, 0};      // Green LED.
inline constexpr gpio::PinId kLd2{gpio::Port::kE, 1};      // Yellow LED.
inline constexpr gpio::PinId kButton{gpio::Port::kC, 13};  // B1, high when pressed.

using Ld1 = gpio::Pin<kLd1>;
using Ld2 = gpio::Pin<kLd2>;
using Button = gpio::Pin<kButton>;
using ButtonInterrupt = exti::PinInterrupt<kButton>;

}  // namespace board

#endif  // EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PINS_
