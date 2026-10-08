#ifndef APP_INCLUDE_UART_USART_
#define APP_INCLUDE_UART_USART_

#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "dma/types.h"
#include "hal/gpio.h"
#include "uart/detail/instance_traits.h"
#include "uart/types.h"

namespace uart {

// Compile-time description of one board port, see uart/ports.h.
template <typename P>
concept PortDescriptor = requires {
  { P::kInstance } -> std::convertible_to<Instance>;
  typename P::RxDma;
  typename P::TxDma;
  { P::kTx } -> std::convertible_to<hal::Pin>;
  { P::kRx } -> std::convertible_to<hal::Pin>;
  { P::kRts } -> std::convertible_to<std::optional<hal::Pin>>;
  { P::kCts } -> std::convertible_to<std::optional<hal::Pin>>;
  { P::kIrqPriority } -> std::convertible_to<std::uint32_t>;
  { P::kRxBufferSize } -> std::convertible_to<std::size_t>;
  { P::kTxBufferSize } -> std::convertible_to<std::size_t>;
  { P::RxBuffer() } -> std::same_as<std::span<std::uint8_t>>;
  { P::TxBuffer() } -> std::same_as<std::span<std::uint8_t>>;
};

// One U(S)ART port. Reception runs continuously through circular DMA into the
// port's RX ring buffer; transmission copies into the port's TX staging buffer
// and sends it with DMA. All state is static: one instantiation per port.
//
// The member functions are defined in uart/detail/usart_impl.h and explicitly
// instantiated per port (app/source/uart/<port>.cc), which also defines the
// USARTx_IRQHandler. Those translation units are always optimized (groov).
//
// Read() must be called by a single consumer and Write() by a single
// producer at a time; Configure() must not run concurrently with either.
// ThreadxSerial<Port> provides that locking. The callback runs in interrupt
// context.
template <PortDescriptor Port>
class Usart {
 public:
  using Traits = detail::InstanceTraits<Port::kInstance>;

  // The ring indices are free-running 32-bit counters, so the RX size must
  // divide 2^32. DMA transfers at most 65535 items.
  static_assert(std::has_single_bit(Port::kRxBufferSize) &&
                    Port::kRxBufferSize <= 32768,
                "RX buffer size must be a power of two <= 32768");
  static_assert(Port::kTxBufferSize > 0 && Port::kTxBufferSize <= 65535);

  Usart() = delete;

  // Enables clocks, configures pins and enables the USART and DMA interrupts
  // (all at Port::kIrqPriority). Call once before Configure().
  static void Init() noexcept;

  static void SetCallback(Callback callback) noexcept { callback_ = callback; }

  // Stops any transfer, programs the line settings and restarts reception.
  // Received data that was not read yet is discarded.
  static std::expected<void, Error> Configure(const Config& config) noexcept;

  // Copies up to data.size() received bytes. Never blocks.
  static std::size_t Read(std::span<std::uint8_t> data) noexcept;

  // Bytes received and not read yet (capped at the ring size).
  static std::size_t Available() noexcept;

  // Starts sending up to Port::kTxBufferSize bytes and returns how many were
  // accepted. Fails with kBusy while the previous transfer is running.
  static std::expected<std::size_t, Error> Write(
      std::span<const std::uint8_t> data) noexcept;

  static bool IsTxBusy() noexcept;

  // Called from USARTx_IRQHandler().
  static void HandleIrq() noexcept;

 private:
  static Event UpdateRxHead() noexcept;
  static void OnRxDma(void* context, dma::Event events) noexcept;
  static void OnTxDma(void* context, dma::Event events) noexcept;

  inline static Callback callback_{};
  // Written only by interrupt handlers of the same priority.
  inline static std::atomic<std::uint32_t> rx_head_{0};
  inline static std::uint32_t rx_dma_position_ = 0;
  // Written only by the reader.
  inline static std::atomic<std::uint32_t> rx_tail_{0};
  inline static std::atomic<bool> tx_busy_{false};
};

}  // namespace uart

#endif  // APP_INCLUDE_UART_USART_
