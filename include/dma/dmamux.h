#ifndef STM32H7_DRIVERS_INCLUDE_DMA_DMAMUX_
#define STM32H7_DRIVERS_INCLUDE_DMA_DMAMUX_

#include <groov/groov.hpp>

#include <cstddef>
#include <utility>

#include "dma/types.h"
#include "hal/detail/ct_name.h"
#include "stm32h7/regs/dmamux1.h"

namespace dma::dmamux1 {

// Connects a DMAMUX1 channel to a peripheral request line, with request
// synchronization and event generation disabled: one store (every field of
// CxCR is written).
template <std::size_t Channel>
void Route(Request request) noexcept {
  static_assert(Channel < 16, "DMAMUX1 has channels 0..15");
  static constexpr auto kCr = hal::detail::Name<"c", Channel, "cr">();

  groov::sync_write(stm32h7::regs::dmamux1::kDmamux1(
      groov::path<kCr, "dmareq_id">{} = std::to_underlying(request),
      groov::path<kCr, "soie">{} = false, groov::path<kCr, "ege">{} = false,
      groov::path<kCr, "se">{} = false, groov::path<kCr, "spol">{} = 0U,
      groov::path<kCr, "nbreq">{} = 0U, groov::path<kCr, "sync_id">{} = 0U));
}

}  // namespace dma::dmamux1

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_DMAMUX_
