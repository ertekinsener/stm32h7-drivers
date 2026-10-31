#ifndef STM32H7_DRIVERS_INCLUDE_DMA_BUFFER_
#define STM32H7_DRIVERS_INCLUDE_DMA_BUFFER_

#include <cstdint>

// Output section of DMA_BUFFER. The default .dma_buffer is placed by the
// application's linker script; the mcu-cmake module sets the section of the
// chip region given as DMA_BUFFER_REGION (e.g. .ram_d2).
#ifndef STM32H7_DRIVERS_DMA_BUFFER_SECTION
#define STM32H7_DRIVERS_DMA_BUFFER_SECTION ".dma_buffer"
#endif

// Places a buffer in memory that DMA1/DMA2 can reach (D2 SRAM1 in the
// reference application) and the MPU maps as non-cacheable (see MPU_Config()
// in main.c), so no cache maintenance is needed.
// Usage: DMA_BUFFER std::array<std::uint8_t, 256> rx_buffer;
#define DMA_BUFFER \
  [[gnu::section(STM32H7_DRIVERS_DMA_BUFFER_SECTION), gnu::aligned(32)]]

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

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_BUFFER_
