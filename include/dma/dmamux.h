#ifndef APP_INCLUDE_DMA_DMAMUX_
#define APP_INCLUDE_DMA_DMAMUX_

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstddef>
#include <utility>

#include "dma/detail/stream_traits.h"
#include "dma/dmamux1_regs.h"
#include "dma/types.h"

namespace dma::dmamux1 {

// Connects a DMAMUX1 channel to a peripheral request line, with request
// synchronization and event generation disabled.
template <std::size_t Channel>
void Route(Request request) noexcept {
  static_assert(Channel < 16, "DMAMUX1 has channels 0..15");
  static constexpr auto kCr = stdx::ct_string{"dmamux1_c"} +
                              detail::ToCtString<Channel>() +
                              stdx::ct_string{"cr"};

  groov::sync_write(stm32h7::dmamux1(
      groov::path<kCr, "dmareq_id">{} = std::to_underlying(request),
      groov::path<kCr, "soie">{} = false, groov::path<kCr, "ege">{} = false,
      groov::path<kCr, "se">{} = false, groov::path<kCr, "spol">{} = 0U,
      groov::path<kCr, "nbreq">{} = 0U, groov::path<kCr, "sync_id">{} = 0U));
}

}  // namespace dma::dmamux1

#endif  // APP_INCLUDE_DMA_DMAMUX_
