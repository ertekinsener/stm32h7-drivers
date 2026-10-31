#include "dma/stream.h"
#include "hal/rng.h"
#include "pins.h"

// Uses the drivers only through their public headers; the stream
// instantiations come from the STM32H7Drivers::DMA::1::0 target.
void ExampleStreamIrq() { dma::Stream<dma::Controller::kDma1, 0>::HandleIrq(); }

// A pin and a pin interrupt through their instances (pins.cc), and a pin
// chosen at runtime.
void ExamplePins() {
  Led::ConfigureOutput();
  Led::Toggle();
  ButtonInterrupt::Configure(exti::Edge::kRising, {}, 8);
  gpio::Configure({gpio::Port::kE, 1}, {.mode = gpio::Mode::kOutput});
}
