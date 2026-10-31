// EXTI driver instances of the board's pin interrupts, built with -O2
// (INSTANCES of stm32h7_drivers_add_exti()).

#include "board/pins.h"
#include "exti/detail/exti_impl.h"

template class exti::PinInterrupt<board::kButton>;
