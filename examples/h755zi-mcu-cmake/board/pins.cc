// GPIO driver instances of the board's pins, built with -O2 (INSTANCES of
// stm32h7_drivers_add_gpio()).

#include "board/pins.h"
#include "gpio/detail/pin_impl.h"

template class gpio::Pin<board::kLd1>;
template class gpio::Pin<board::kLd2>;
template class gpio::Pin<board::kButton>;
