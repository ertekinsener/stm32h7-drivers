#ifndef APP_INCLUDE_DMA_BUFFER_
#define APP_INCLUDE_DMA_BUFFER_

#include <cstdint>

// Places a buffer in D2 SRAM1, which DMA1/DMA2 can reach and the MPU maps as
// non-cacheable (see MPU_Config() in main.c), so no cache maintenance is
// needed. Usage: DMA_BUFFER std::array<std::uint8_t, 256> rx_buffer;
#define DMA_BUFFER [[gnu::section(".dma_buffer"), gnu::aligned(32)]]

namespace dma {

// DMA1/DMA2 sit on the D2 AHB matrix and cannot access the Cortex-M7 tightly
// coupled memories (RM0399, "Bus matrix").
constexpr bool IsDmaReachable(std::uintptr_t address) noexcept {
  constexpr std::uintptr_t kItcmEnd = 0x0001'0000U;
  constexpr std::uintptr_t kDtcmBegin = 0x2000'0000U;
  constexpr std::uintptr_t kDtcmEnd = 0x2002'0000U;
  return address >= kItcmEnd &&
         !(address >= kDtcmBegin && address < kDtcmEnd);
}

}  // namespace dma

#endif  // APP_INCLUDE_DMA_BUFFER_
