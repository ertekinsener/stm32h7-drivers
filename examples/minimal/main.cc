#include "dma/stream.h"
#include "hal/rng.h"

// Uses the drivers only through their public headers; the stream
// instantiations come from the STM32H7Drivers::DMA::1::0 target.
void ExampleStreamIrq() { dma::Stream<dma::Controller::kDma1, 0>::HandleIrq(); }
