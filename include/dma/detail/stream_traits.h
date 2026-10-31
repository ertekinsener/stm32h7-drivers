#ifndef STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_TRAITS_
#define STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_TRAITS_

#include <stdx/ct_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

#include "dma/types.h"
#include "hal/detail/ct_name.h"
#include "stm32h7/irq.h"
#include "stm32h7/regs/dma.h"

namespace dma::detail {

using hal::detail::Name;

// Compile-time description of one DMA stream: its groov group, the names of
// its registers and flag fields, its DMAMUX1 channel and its IRQ number.
template <Controller C, std::uint8_t N>
struct StreamTraits {
  static_assert(N < 8, "DMA1/DMA2 have streams 0..7");

  static constexpr auto kGroup = [] {
    if constexpr (C == Controller::kDma1) {
      return stm32h7::regs::dma::kDma1;
    } else {
      return stm32h7::regs::dma::kDma2;
    }
  }();

  static constexpr auto kCr = Name<"s", N, "cr">();
  static constexpr auto kNdtr = Name<"s", N, "ndtr">();
  static constexpr auto kPar = Name<"s", N, "par">();
  static constexpr auto kM0ar = Name<"s", N, "m0ar">();
  static constexpr auto kFcr = Name<"s", N, "fcr">();

  // Streams 0..3 report in LISR/LIFCR, streams 4..7 in HISR/HIFCR.
  static constexpr stdx::ct_string kFlagReg = N < 4 ? "lisr" : "hisr";
  static constexpr stdx::ct_string kClearReg = N < 4 ? "lifcr" : "hifcr";

  static constexpr auto kTcif = Name<"tcif", N>();
  static constexpr auto kHtif = Name<"htif", N>();
  static constexpr auto kTeif = Name<"teif", N>();
  static constexpr auto kDmeif = Name<"dmeif", N>();
  static constexpr auto kFeif = Name<"feif", N>();

  static constexpr auto kCtcif = stdx::ct_string{"c"} + kTcif;
  static constexpr auto kChtif = stdx::ct_string{"c"} + kHtif;
  static constexpr auto kCteif = stdx::ct_string{"c"} + kTeif;
  static constexpr auto kCdmeif = stdx::ct_string{"c"} + kDmeif;
  static constexpr auto kCfeif = stdx::ct_string{"c"} + kFeif;

  // DMAMUX1 channels 0..7 feed DMA1 streams 0..7, channels 8..15 DMA2.
  static constexpr std::size_t kMuxChannel =
      static_cast<std::size_t>(C) * 8U + N;

  static constexpr stm32h7::Irq kIrq = [] {
    using enum stm32h7::Irq;
    constexpr std::array kDma1{kDma1Stream0, kDma1Stream1, kDma1Stream2,
                               kDma1Stream3, kDma1Stream4, kDma1Stream5,
                               kDma1Stream6, kDma1Stream7};
    constexpr std::array kDma2{kDma2Stream0, kDma2Stream1, kDma2Stream2,
                               kDma2Stream3, kDma2Stream4, kDma2Stream5,
                               kDma2Stream6, kDma2Stream7};
    return C == Controller::kDma1 ? kDma1[N] : kDma2[N];
  }();

  // Clock enable field in RCC_AHB1ENR.
  static constexpr stdx::ct_string kClockEnable =
      C == Controller::kDma1 ? "dma1en" : "dma2en";
};

}  // namespace dma::detail

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_TRAITS_
