#ifndef STM32H7_DRIVERS_INCLUDE_UART_USART_
#define STM32H7_DRIVERS_INCLUDE_UART_USART_

#include <algorithm>
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
#include "ring/mpsc_ring.h"
#include "uart/detail/instance_traits.h"
#include "uart/types.h"

namespace uart {

// Compile-time description of one board port (application code, see
// docs/uart/README.md, "Ports of an application").
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
// port's RX ring buffer. Transmission is queued: Write() appends to a
// lock-free TX ring (ring::MpscRing over the port's TX buffer) and the driver
// sends every committed record behind the tail with one DMA transfer. All
// state is static: one instantiation per port.
//
// The TX queue is drained only at the port's interrupt priority: by the TX
// DMA completion handler, and by HandleIrq(), which Write() pends through
// the NVIC when no transfer is running. So writers never touch the DMA
// stream and never wait.
//
// The member functions are defined in uart/detail/usart_impl.h and explicitly
// instantiated per port (the application's <port>.cc), which also defines the
// USARTx_IRQHandler. Those translation units are always optimized (groov).
//
// Write() is safe from any context on this core, including interrupts of any
// priority and concurrently with Configure(). Read() must be called by a
// single consumer, and Configure() must not run concurrently with Read() or
// itself; ThreadxSerial<Port> provides that locking. The callback runs in
// interrupt context.
template <PortDescriptor Port>
class Usart {
 public:
  using Traits = detail::InstanceTraits<Port::kInstance>;

  // The ring indices are free-running 32-bit counters, so the RX size must
  // divide 2^32. DMA transfers at most 65535 items.
  static_assert(std::has_single_bit(Port::kRxBufferSize) &&
                    Port::kRxBufferSize <= 32768,
                "RX buffer size must be a power of two <= 32768");

  // Assumes log-sized records (about 32 bytes on average) for the number of
  // TX record slots; a full slot table rejects writes like a full buffer.
  using TxRing =
      ring::MpscRing<Port::kTxBufferSize,
                     std::max<std::size_t>(Port::kTxBufferSize / 32, 8)>;

  // Largest Write(). An empty queue always accepts it.
  static constexpr std::size_t kMaxWriteSize = TxRing::kMaxRecordSize;

  Usart() = delete;

  // Enables clocks, configures pins and enables the USART and DMA interrupts
  // (all at Port::kIrqPriority). Call once before Configure().
  static void Init() noexcept;

  static void SetCallback(Callback callback) noexcept { callback_ = callback; }

  // Programs the line settings and restarts reception. A running TX batch is
  // allowed to finish first (it is aborted if it makes no progress); queued
  // TX data is kept and sent with the new settings. Received data that was
  // not read yet is discarded. Transmission starts with the first successful
  // call and stops if a later call fails after touching the hardware.
  static std::expected<void, Error> Configure(const Config& config) noexcept;

  // Copies up to data.size() received bytes. Never blocks. Bytes the DMA
  // overwrote before or while they were copied are skipped and reported to
  // the callback as kRxBufferOverflow (from the caller's context).
  static std::size_t Read(std::span<std::uint8_t> data) noexcept;

  // Bytes received and not read yet (capped at the ring size).
  static std::size_t Available() noexcept;

  // Queues all of `data` as one unit (it is never interleaved with other
  // writes) and returns without waiting for the transfer. Fails with
  // kTooLarge above kMaxWriteSize and kNoSpace when the queue is full, and
  // with kNoSpace before Init(). Lock-free; safe from any context.
  static std::expected<void, Error> Write(
      std::span<const std::uint8_t> data) noexcept;

  // True while TX data is queued or being sent.
  static bool IsTxBusy() noexcept;

  // Called from USARTx_IRQHandler().
  static void HandleIrq() noexcept;

 private:
  static Event UpdateRxHead() noexcept;
  static std::uint32_t DmaHead() noexcept;
  static void StartTx() noexcept;
  static void OnRxDma(void* context, dma::Event events) noexcept;
  static void OnTxDma(void* context, dma::Event events) noexcept;

  inline static Callback callback_{};
  // Written only by interrupt handlers of the same priority.
  inline static std::atomic<std::uint32_t> rx_head_{0};
  inline static std::uint32_t rx_dma_position_ = 0;
  // Written only by the reader.
  inline static std::atomic<std::uint32_t> rx_tail_{0};

  inline static TxRing tx_ring_{};
  // The batch being sent. Changed only at the port's interrupt priority, or
  // by Configure() while that cannot run (see Configure()).
  inline static typename TxRing::Batch tx_batch_{};
  inline static std::atomic<bool> tx_in_flight_{false};
  // Set until the first successful Configure(), and while it reconfigures.
  inline static std::atomic<bool> tx_paused_{true};
};

}  // namespace uart

#endif  // STM32H7_DRIVERS_INCLUDE_UART_USART_
