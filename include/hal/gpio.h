#ifndef STM32H7_DRIVERS_INCLUDE_HAL_GPIO_
#define STM32H7_DRIVERS_INCLUDE_HAL_GPIO_

#include <cstdint>

#include "gpio/gpio.h"

namespace hal {

// Compatibility with the first GPIO helpers, used by the U(S)ART port
// descriptors: a pin together with its alternate function. New code uses
// the gpio driver (gpio/gpio.h); these forward to it.

using gpio::Port;
using gpio::Pull;
using gpio::Speed;

struct Pin {
  Port port;
  std::uint8_t number;     // 0..15
  std::uint8_t alternate;  // AF0..AF15
};

// Enables the port clock and puts the pin in push-pull alternate-function
// mode.
void ConfigureAlternate(Pin pin, Pull pull, Speed speed) noexcept;

// Enables the port clock and makes the pin an input (`alternate` unused).
void ConfigureInput(Pin pin, Pull pull) noexcept;

// The input level of the pin.
bool Read(Pin pin) noexcept;

}  // namespace hal

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_GPIO_
