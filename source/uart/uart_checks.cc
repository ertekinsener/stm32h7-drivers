// Compile-time checks of the UART instance descriptions against RM0399
// (memory map, NVIC vector table) and of the baud rate arithmetic. No
// runtime code.

#include <utility>

#include "dma/types.h"
#include "uart/detail/baud.h"
#include "uart/detail/instance_traits.h"

namespace {

using uart::Instance;
using uart::Oversampling;
using uart::detail::ComputeBrr;
using uart::detail::InstanceTraits;

static_assert(InstanceTraits<Instance::kUsart3>::kBase == 0x4000'4800U);
static_assert(std::to_underlying(InstanceTraits<Instance::kUsart3>::kIrq) ==
              39);
static_assert(InstanceTraits<Instance::kUsart3>::kRxRequest ==
              dma::Request::kUsart3Rx);
static_assert(InstanceTraits<Instance::kUsart1>::kInfo.domain ==
              uart::detail::ClockDomain::kApb2);
static_assert(std::to_underlying(InstanceTraits<Instance::kUart8>::kIrq) ==
              83);
static_assert(std::to_underlying(InstanceTraits<Instance::kUsart6>::kIrq) ==
              71);
static_assert(InstanceTraits<Instance::kUsart6>::kBase == 0x4001'1400U);

// 100 MHz kernel clock (D2 PCLK1).
static_assert(ComputeBrr(100'000'000, 115'200, Oversampling::kBy16) == 868);
static_assert(ComputeBrr(100'000'000, 921'600, Oversampling::kBy16) == 109);
// OVER8: USARTDIV = 217 = 0xD9 -> BRR = 0xD0 | (0x9 >> 1) = 0xD4.
static_assert(ComputeBrr(100'000'000, 921'600, Oversampling::kBy8) == 0xD4);
// 12.5 Mbaud: USARTDIV 8 < 16 with OVER16, but 16 with OVER8.
static_assert(!ComputeBrr(100'000'000, 12'500'000, Oversampling::kBy16));
static_assert(ComputeBrr(100'000'000, 12'500'000, Oversampling::kBy8) == 0x10);
// Too slow: USARTDIV > 0xFFFF.
static_assert(!ComputeBrr(100'000'000, 1'200, Oversampling::kBy16));
static_assert(!ComputeBrr(100'000'000, 0, Oversampling::kBy16));

}  // namespace
