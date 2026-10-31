#ifndef EXAMPLES_MINIMAL_PINS_
#define EXAMPLES_MINIMAL_PINS_

#include "exti/exti.h"
#include "gpio/gpio.h"

inline constexpr gpio::PinId kLed{gpio::Port::kB, 0};
inline constexpr gpio::PinId kButton{gpio::Port::kC, 13};
using Led = gpio::Pin<kLed>;
using ButtonInterrupt = exti::PinInterrupt<kButton>;

#endif  // EXAMPLES_MINIMAL_PINS_
