#ifndef EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PORTS_
#define EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PORTS_

// UART ports of the example. Each port's driver instance is compiled in its
// own translation unit (board/usart3_vcp.cc, PORTS of
// stm32h7_drivers_add_uart()); its DMA streams must be in STREAMS of
// stm32h7_drivers_add_dma().

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "dma/stream.h"
#include "hal/gpio.h"
#include "uart/types.h"
#include "uart/usart.h"

namespace uart {

// USART3 on PD8 (TX) / PD9 (RX), AF7: the ST-LINK virtual COM port of the
// NUCLEO-H755ZI-Q.
struct Usart3Vcp {
  static constexpr char kName[] = "USART3";
  static constexpr Instance kInstance = Instance::kUsart3;
  using RxDma = dma::Stream<dma::Controller::kDma1, 0>;
  using TxDma = dma::Stream<dma::Controller::kDma1, 1>;

  static constexpr hal::Pin kTx{hal::Port::kD, 8, 7};
  static constexpr hal::Pin kRx{hal::Port::kD, 9, 7};
  static constexpr std::optional<hal::Pin> kRts{};
  static constexpr std::optional<hal::Pin> kCts{};

  static constexpr std::uint32_t kIrqPriority = 6;
  static constexpr std::size_t kRxBufferSize = 512;
  static constexpr std::size_t kTxBufferSize = 1024;

  static std::span<std::uint8_t> RxBuffer() noexcept;
  static std::span<std::uint8_t> TxBuffer() noexcept;
};

extern template class Usart<Usart3Vcp>;

}  // namespace uart

#endif  // EXAMPLES_H755ZI_MCU_CMAKE_BOARD_PORTS_
