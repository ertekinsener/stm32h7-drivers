#include <cstdint>
#include <string_view>
#include <utility>

#include "dma/detail/stream_traits.h"

// Compile-time checks of the stream descriptions against RM0399 (NVIC vector
// table, DMAMUX1 channels, register names). No runtime code.
namespace {

using dma::Controller;
using dma::detail::StreamTraits;

template <Controller C, std::uint8_t N>
constexpr auto kIrqNumber = std::to_underlying(StreamTraits<C, N>::kIrq);

static_assert(kIrqNumber<Controller::kDma1, 0> == 11);
static_assert(kIrqNumber<Controller::kDma1, 6> == 17);
static_assert(kIrqNumber<Controller::kDma1, 7> == 47);
static_assert(kIrqNumber<Controller::kDma2, 0> == 56);
static_assert(kIrqNumber<Controller::kDma2, 4> == 60);
static_assert(kIrqNumber<Controller::kDma2, 5> == 68);
static_assert(kIrqNumber<Controller::kDma2, 7> == 70);

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
static_assert(std::string_view{hal::detail::Name<"ip", 149>()} == "ip149");

// Base addresses of the maps (RM0399, memory map).
static_assert(stm32h7::regs::dma::kDma2Base == 0x4002'0400U);

}  // namespace
