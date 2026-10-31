// USART3 / ST-LINK virtual COM port: buffers, driver instantiation and
// interrupt vector. Built optimized (PORTS of stm32h7_drivers_add_uart()).

#include <array>
#include <cstdint>
#include <span>

#include "board/ports.h"
#include "dma/buffer.h"
#include "uart/detail/usart_impl.h"

namespace uart {
namespace {

DMA_BUFFER std::array<std::uint8_t, Usart3Vcp::kRxBufferSize> rx_buffer;
DMA_BUFFER std::array<std::uint8_t, Usart3Vcp::kTxBufferSize> tx_buffer;

}  // namespace

std::span<std::uint8_t> Usart3Vcp::RxBuffer() noexcept { return rx_buffer; }
std::span<std::uint8_t> Usart3Vcp::TxBuffer() noexcept { return tx_buffer; }

template class Usart<Usart3Vcp>;

}  // namespace uart

extern "C" void USART3_IRQHandler() {
  uart::Usart<uart::Usart3Vcp>::HandleIrq();
}
