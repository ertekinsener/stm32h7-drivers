// Driver instances of the pins (groov code, built with -O2).

#include "exti/detail/exti_impl.h"
#include "gpio/detail/pin_impl.h"
#include "pins.h"

template class gpio::Pin<kLed>;
template class exti::PinInterrupt<kButton>;
