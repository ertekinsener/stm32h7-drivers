#ifndef APP_INCLUDE_DMA_DETAIL_STREAM_TRAITS_
#define APP_INCLUDE_DMA_DETAIL_STREAM_TRAITS_

#include <stdx/ct_string.hpp>

#include <cstddef>
#include <cstdint>

#include "dma/dma1_regs.h"
#include "dma/dma2_regs.h"
#include "dma/types.h"
#include "stm32h7xx.h"

namespace dma::detail {

// Decimal representation of Value as a compile-time string, e.g. 12 -> "12".
template <std::size_t Value>
consteval auto ToCtString() {
  static_assert(Value < 100);
  constexpr std::size_t kDigits = Value < 10 ? 1 : 2;
  char digits[kDigits]{};
  if constexpr (kDigits == 2) {
    digits[0] = static_cast<char>('0' + Value / 10);
  }
  digits[kDigits - 1] = static_cast<char>('0' + Value % 10);
  return stdx::ct_string<kDigits + 1>{digits, kDigits};
}

// Name of a stream register, e.g. StreamRegister<3>("cr") -> "s3cr".
template <std::uint8_t N, std::size_t S>
consteval auto StreamRegister(const char (&suffix)[S]) {
  return stdx::ct_string{"s"} + ToCtString<N>() + stdx::ct_string<S>{suffix};
}

// Compile-time description of one DMA stream: its groov group, the names of
// its registers and flag fields, its DMAMUX1 channel and its IRQ number.
template <Controller C, std::uint8_t N>
struct StreamTraits {
  static_assert(N < 8, "DMA1/DMA2 have streams 0..7");

  static constexpr auto kGroup = [] {
    if constexpr (C == Controller::kDma1) {
      return stm32h7::dma1;
    } else {
      return stm32h7::dma2;
    }
  }();

  static constexpr auto kIndex = ToCtString<N>();

  static constexpr auto kCr = StreamRegister<N>("cr");
  static constexpr auto kNdtr = StreamRegister<N>("ndtr");
  static constexpr auto kPar = StreamRegister<N>("par");
  static constexpr auto kM0ar = StreamRegister<N>("m0ar");
  static constexpr auto kFcr = StreamRegister<N>("fcr");

  // Streams 0..3 report in LISR/LIFCR, streams 4..7 in HISR/HIFCR.
  static constexpr stdx::ct_string kFlagReg = N < 4 ? "lisr" : "hisr";
  static constexpr stdx::ct_string kClearReg = N < 4 ? "lifcr" : "hifcr";

  static constexpr auto kTcif = stdx::ct_string{"tcif"} + kIndex;
  static constexpr auto kHtif = stdx::ct_string{"htif"} + kIndex;
  static constexpr auto kTeif = stdx::ct_string{"teif"} + kIndex;
  static constexpr auto kDmeif = stdx::ct_string{"dmeif"} + kIndex;
  static constexpr auto kFeif = stdx::ct_string{"feif"} + kIndex;

  static constexpr auto kCtcif = stdx::ct_string{"c"} + kTcif;
  static constexpr auto kChtif = stdx::ct_string{"c"} + kHtif;
  static constexpr auto kCteif = stdx::ct_string{"c"} + kTeif;
  static constexpr auto kCdmeif = stdx::ct_string{"c"} + kDmeif;
  static constexpr auto kCfeif = stdx::ct_string{"c"} + kFeif;

  // DMAMUX1 channels 0..7 feed DMA1 streams 0..7, channels 8..15 DMA2.
  static constexpr std::size_t kMuxChannel =
      static_cast<std::size_t>(C) * 8U + N;

  static constexpr IRQn_Type kIrq = [] {
    if constexpr (C == Controller::kDma1) {
      return N < 7 ? static_cast<IRQn_Type>(DMA1_Stream0_IRQn + N)
                   : DMA1_Stream7_IRQn;
    } else {
      return N < 5 ? static_cast<IRQn_Type>(DMA2_Stream0_IRQn + N)
                   : static_cast<IRQn_Type>(DMA2_Stream5_IRQn + (N - 5));
    }
  }();

  static constexpr std::uint32_t kClockEnable =
      C == Controller::kDma1 ? RCC_AHB1ENR_DMA1EN : RCC_AHB1ENR_DMA2EN;
};

}  // namespace dma::detail

#endif  // APP_INCLUDE_DMA_DETAIL_STREAM_TRAITS_
