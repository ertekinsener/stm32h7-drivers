#include <string_view>

#include "dma/detail/stream_traits.h"

// Compile-time checks of the stream descriptions against the CMSIS device
// header and RM0399.
namespace {

using dma::Controller;
using dma::detail::StreamTraits;

static_assert(StreamTraits<Controller::kDma1, 0>::kIrq == DMA1_Stream0_IRQn);
static_assert(StreamTraits<Controller::kDma1, 6>::kIrq == DMA1_Stream6_IRQn);
static_assert(StreamTraits<Controller::kDma1, 7>::kIrq == DMA1_Stream7_IRQn);
static_assert(StreamTraits<Controller::kDma2, 4>::kIrq == DMA2_Stream4_IRQn);
static_assert(StreamTraits<Controller::kDma2, 5>::kIrq == DMA2_Stream5_IRQn);
static_assert(StreamTraits<Controller::kDma2, 7>::kIrq == DMA2_Stream7_IRQn);

static_assert(StreamTraits<Controller::kDma1, 7>::kMuxChannel == 7);
static_assert(StreamTraits<Controller::kDma2, 0>::kMuxChannel == 8);
static_assert(StreamTraits<Controller::kDma2, 7>::kMuxChannel == 15);

static_assert(std::string_view{StreamTraits<Controller::kDma1, 3>::kFlagReg} ==
              "lisr");
static_assert(std::string_view{StreamTraits<Controller::kDma1, 4>::kFlagReg} ==
              "hisr");
static_assert(std::string_view{StreamTraits<Controller::kDma2, 5>::kCr} ==
              "s5cr");
static_assert(std::string_view{StreamTraits<Controller::kDma2, 5>::kCdmeif} ==
              "cdmeif5");

}  // namespace
