#ifndef STM32H7_DRIVERS_INCLUDE_HAL_GPIO_
#define STM32H7_DRIVERS_INCLUDE_HAL_GPIO_

#include <cstdint>

namespace hal {

// GPIO pins. A pin known at compile time is configured with
// ConfigureAlternate<pin>() (hal/detail/gpio_impl.h, groov with named fields,
// used by the drivers); the functions here take a pin at runtime.

enum class Port : std::uint8_t { kA, kB, kC, kD, kE, kF, kG, kH, kI, kJ, kK };

enum class Pull : std::uint8_t { kNone = 0b00, kUp = 0b01, kDown = 0b10 };

enum class Speed : std::uint8_t {
  kLow = 0b00,
  kMedium = 0b01,
  kHigh = 0b10,
  kVeryHigh = 0b11,
};

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
