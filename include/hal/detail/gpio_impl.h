#ifndef STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_
#define STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_

// Compile-time configuration of a hal::Pin (pin and alternate function), on
// the gpio driver. Driver-internal: for sources built with -O2.

#include "gpio/detail/pin_impl.h"
#include "hal/gpio.h"

namespace hal {

// Enables the port clock and puts the pin in push-pull alternate-function
// mode (gpio::Pin::ConfigureAlternate()).
template <Pin P>
void ConfigureAlternate(Pull pull, Speed speed) noexcept {
  static_assert(P.number < 16 && P.alternate < 16);
  gpio::Pin<gpio::PinId{P.port, P.number}>::ConfigureAlternate(
      P.alternate, gpio::OutputType::kPushPull, speed, pull);
}

}  // namespace hal

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_
