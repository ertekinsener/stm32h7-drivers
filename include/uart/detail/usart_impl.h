#ifndef STM32H7_DRIVERS_INCLUDE_UART_DETAIL_USART_IMPL_
#define STM32H7_DRIVERS_INCLUDE_UART_DETAIL_USART_IMPL_

// Definitions of uart::Usart members, with groov on the generated register
// map (stm32h7::regs::usart). Only the per-port translation units (the
// application's <port>.cc, see docs/uart/README.md) include this file; they
// are built with -O2, where groov collapses into plain loads and stores.
// Everything else uses the extern template declarations next to the
// application's port descriptors.

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <expected>
#include <utility>

#include "dma/stream.h"
#include "hal/cortex_m.h"
#include "hal/detail/gpio_impl.h"
#include "stm32h7/regs/usart.h"
#include "uart/detail/baud.h"
#include "uart/usart.h"

namespace uart {
namespace detail {

inline constexpr std::uint32_t kTxDrainTimeout = 1'000'000;
inline constexpr std::uintptr_t kRdrOffset = 0x24;
inline constexpr std::uintptr_t kTdrOffset = 0x28;

// groov paths of the USART registers used by the driver.
template <std::uintptr_t Base>
struct Paths {
  static constexpr auto kGroup = stm32h7::regs::usart::kMap<Base>;

  template <stdx::ct_string Field>
  static constexpr groov::path<"cr1", Field> kCr1{};
  template <stdx::ct_string Field>
  static constexpr groov::path<"cr2", Field> kCr2{};
  template <stdx::ct_string Field>
  static constexpr groov::path<"cr3", Field> kCr3{};
  template <stdx::ct_string Field>
  static constexpr groov::path<"isr", Field> kIsr{};
  template <stdx::ct_string Field>
  static constexpr groov::path<"icr", Field> kIcr{};
  static constexpr groov::path<"brr"> kBrr{};
  static constexpr groov::path<"presc", "prescaler"> kPresc{};
};

// CR1.M1:M0 for a frame of data bits plus the optional parity bit.
struct FrameBits {
  bool m1;
  bool m0;
};

constexpr FrameBits ToFrameBits(const Config& config) noexcept {
  const bool parity = config.parity != Parity::kNone;
  if (config.data_bits == WordLength::kBits7) {
    return parity ? FrameBits{false, false}   // 8-bit frame
                  : FrameBits{true, false};   // 7-bit frame
  }
  return parity ? FrameBits{false, true}      // 9-bit frame
                : FrameBits{false, false};    // 8-bit frame
}

template <PortDescriptor Port>
constexpr bool IsValid(const Config& config) noexcept {
  const bool rts = config.flow_control == FlowControl::kRts ||
                   config.flow_control == FlowControl::kRtsCts;
  const bool cts = config.flow_control == FlowControl::kCts ||
                   config.flow_control == FlowControl::kRtsCts;
  return (!rts || Port::kRts.has_value()) && (!cts || Port::kCts.has_value());
}

}  // namespace detail

template <PortDescriptor Port>
void Usart<Port>::Init() noexcept {
  Traits::EnableClock();

  // The pull-up holds TX idle (high) while the transmitter is disabled (before
  // Configure() and while it reconfigures), when the USART does not drive
  // the pin; a floating line shows up as garbage at the terminal.
  hal::ConfigureAlternate<Port::kTx>(hal::Pull::kUp, hal::Speed::kMedium);
  // The pull-up keeps an unconnected RX line idle instead of floating.
  hal::ConfigureAlternate<Port::kRx>(hal::Pull::kUp, hal::Speed::kMedium);
  if constexpr (Port::kRts.has_value()) {
    hal::ConfigureAlternate<*Port::kRts>(hal::Pull::kNone, hal::Speed::kMedium);
  }
  if constexpr (Port::kCts.has_value()) {
    hal::ConfigureAlternate<*Port::kCts>(hal::Pull::kNone, hal::Speed::kMedium);
  }

  // Same priority for the USART and both DMA streams: their handlers never
  // preempt each other, so UpdateRxHead() needs no lock.
  Port::RxDma::Init(Port::kIrqPriority);
  Port::TxDma::Init(Port::kIrqPriority);
  hal::cortex_m::SetPriority<Traits::kIrq>(Port::kIrqPriority);
  hal::cortex_m::EnableIrq<Traits::kIrq>();

  tx_ring_.Attach(Port::TxBuffer().template first<Port::kTxBufferSize>());
}

template <PortDescriptor Port>
std::expected<void, Error> Usart<Port>::Configure(
    const Config& config) noexcept {
  using P = detail::Paths<Traits::kBase>;

  const auto brr = detail::ComputeBrr(Traits::KernelClockHz(),
                                      config.baud_rate, config.oversampling);
  if (!brr || !detail::IsValid<Port>(config)) {
    return std::unexpected(Error::kInvalidConfig);
  }

  // Hold back new TX batches and let the running one finish with the old
  // settings, unless it stalls (e.g. CTS held off).
  tx_paused_.store(true);
  std::uint16_t remaining = Port::TxDma::Remaining();
  for (std::uint32_t stalled = 0;
       tx_in_flight_.load() && stalled < detail::kTxDrainTimeout;) {
    const std::uint16_t now = Port::TxDma::Remaining();
    stalled = now == remaining ? stalled + 1 : 0;
    remaining = now;
  }

  // Stop both directions and let the last frame leave the shift register.
  if (!Port::TxDma::Stop() || !Port::RxDma::Stop()) {
    return std::unexpected(Error::kDma);
  }
  // A batch aborted by Stop() may or may not have raised its completion
  // interrupt before Stop() cleared the flags. If it did not, the TX
  // handlers can no longer run, so the batch is dropped here.
  if (tx_in_flight_.load()) {
    tx_ring_.Release(tx_batch_);
    tx_in_flight_.store(false);
  }
  const bool enabled = groov::sync_read(P::kGroup / P::template kCr1<"ue">);
  if (enabled) {
    for (std::uint32_t i = 0;
         !groov::sync_read(P::kGroup / P::template kIsr<"tc">); ++i) {
      if (i == detail::kTxDrainTimeout) {
        return std::unexpected(Error::kTimeout);
      }
    }
  }
  groov::sync_write(P::kGroup(P::template kCr1<"ue"> = false));

  const auto frame = detail::ToFrameBits(config);
  const bool parity = config.parity != Parity::kNone;
  const bool rts = config.flow_control == FlowControl::kRts ||
                   config.flow_control == FlowControl::kRtsCts;
  const bool cts = config.flow_control == FlowControl::kCts ||
                   config.flow_control == FlowControl::kRtsCts;

  groov::sync_write(P::kGroup(
      P::template kCr1<"fifoen"> = config.fifo,
      P::template kCr1<"m1"> = frame.m1, P::template kCr1<"m0"> = frame.m0,
      P::template kCr1<"over8"> = config.oversampling == Oversampling::kBy8,
      P::template kCr1<"pce"> = parity,
      P::template kCr1<"ps"> = config.parity == Parity::kOdd,
      P::template kCr1<"peie"> = parity, P::template kCr1<"idleie"> = true,
      P::template kCr1<"rxneie"> = false, P::template kCr1<"txeie"> = false,
      P::template kCr1<"tcie"> = false, P::template kCr1<"rxffie"> = false,
      P::template kCr1<"txfeie"> = false, P::template kCr1<"eobie"> = false,
      P::template kCr1<"rtoie"> = false, P::template kCr1<"cmie"> = false,
      P::template kCr1<"mme"> = false, P::template kCr1<"uesm"> = false,
      P::template kCr1<"te"> = true, P::template kCr1<"re"> = true,
      P::template kCr1<"ue"> = false));

  groov::sync_write(P::kGroup(
      P::template kCr2<"stop"> = std::to_underlying(config.stop_bits),
      P::template kCr2<"swap"> = config.swap_tx_rx,
      P::template kCr2<"msbfirst"> = false, P::template kCr2<"txinv"> = false,
      P::template kCr2<"rxinv"> = false, P::template kCr2<"tainv"> = false,
      P::template kCr2<"linen"> = false, P::template kCr2<"clken"> = false,
      P::template kCr2<"abren"> = false, P::template kCr2<"rtoen"> = false));

  // DDRE = 0 keeps RX DMA running after a line error; errors are reported
  // through EIE (FE, ORE, NE) and PEIE.
  groov::sync_write(P::kGroup(
      P::template kCr3<"dmat"> = true, P::template kCr3<"dmar"> = true,
      P::template kCr3<"eie"> = true, P::template kCr3<"ddre"> = false,
      P::template kCr3<"ovrdis"> = false, P::template kCr3<"rtse"> = rts,
      P::template kCr3<"ctse"> = cts, P::template kCr3<"ctsie"> = false,
      P::template kCr3<"hdsel"> = false, P::template kCr3<"scen"> = false,
      P::template kCr3<"iren"> = false, P::template kCr3<"dem"> = false,
      P::template kCr3<"onebit"> = false, P::template kCr3<"rxftie"> = false,
      P::template kCr3<"txftie"> = false));

  groov::sync_write(P::kGroup(P::kBrr = *brr, P::kPresc = 0U));

  const dma::Config rx_dma{.request = Traits::kRxRequest,
                           .direction = dma::Direction::kPeripheralToMemory,
                           .mode = dma::Mode::kCircular,
                           .priority = dma::Priority::kHigh,
                           .bufferable = true,
                           .interrupts = dma::Event::kHalfTransfer |
                                         dma::Event::kTransferComplete |
                                         dma::Event::kTransferError |
                                         dma::Event::kDirectModeError};
  const dma::Config tx_dma{.request = Traits::kTxRequest,
                           .direction = dma::Direction::kMemoryToPeripheral,
                           .priority = dma::Priority::kMedium,
                           .bufferable = true,
                           .interrupts = dma::Event::kTransferComplete |
                                         dma::Event::kTransferError |
                                         dma::Event::kDirectModeError};
  if (!Port::RxDma::Configure(rx_dma) || !Port::TxDma::Configure(tx_dma)) {
    return std::unexpected(Error::kDma);
  }
  Port::RxDma::SetCallback({&OnRxDma, nullptr});
  Port::TxDma::SetCallback({&OnTxDma, nullptr});

  rx_head_.store(0, std::memory_order_relaxed);
  rx_tail_.store(0, std::memory_order_relaxed);
  rx_dma_position_ = 0;

  const std::span<std::uint8_t> ring = Port::RxBuffer();
  if (!Port::RxDma::Start(Traits::kBase + detail::kRdrOffset,
                          reinterpret_cast<std::uintptr_t>(ring.data()),
                          static_cast<std::uint16_t>(ring.size()))) {
    return std::unexpected(Error::kDma);
  }

  // Drop flags left over from the old settings, then enable.
  groov::sync_write(P::kGroup(
      P::template kIcr<"idlecf"> = true, P::template kIcr<"orecf"> = true,
      P::template kIcr<"ncf"> = true, P::template kIcr<"fecf"> = true,
      P::template kIcr<"pecf"> = true, P::template kIcr<"tccf"> = true));
  groov::sync_write(P::kGroup(P::template kCr1<"ue"> = true));

  // Resume TX; HandleIrq() sends whatever was queued meanwhile.
  tx_paused_.store(false);
  hal::cortex_m::SetPendingIrq<Traits::kIrq>();
  return {};
}

template <PortDescriptor Port>
std::size_t Usart<Port>::Read(std::span<std::uint8_t> data) noexcept {
  const std::span<std::uint8_t> ring = Port::RxBuffer();
  const auto size = static_cast<std::uint32_t>(ring.size());

  const std::uint32_t head = rx_head_.load(std::memory_order_acquire);
  std::uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  bool lapped = false;
  if (head - tail > size) {
    tail = head - size;  // Lapped: the oldest data was overwritten.
    lapped = true;
  }

  auto count = static_cast<std::uint32_t>(
      std::min<std::size_t>(head - tail, data.size()));
  const std::uint32_t start = tail % size;
  const std::uint32_t first = std::min(count, size - start);
  std::memcpy(data.data(), ring.data() + start, first);
  std::memcpy(data.data() + first, ring.data(), count - first);

  // rx_head_ lags the DMA by up to half a ring, and the DMA overwrites the
  // oldest bytes from its own position on: only the `size` bytes before
  // DmaHead() are still intact. Checked after copying, so that bytes
  // overwritten during the copy are dropped as well.
  const std::uint32_t oldest = DmaHead() - size;
  if (static_cast<std::int32_t>(oldest - tail) > 0) {
    const std::uint32_t lost = std::min(oldest - tail, count);
    std::memmove(data.data(), data.data() + lost, count - lost);
    count -= lost;
    tail += lost;
    lapped = true;
  }

  rx_tail_.store(tail + count, std::memory_order_release);
  if (lapped) {
    callback_(Event::kRxBufferOverflow);
  }
  return count;
}

// The logical ring index the DMA writes next: rx_head_ plus what the DMA
// wrote since the last UpdateRxHead(). Interrupts are masked so that rx_head_
// and rx_dma_position_ come from the same update.
template <PortDescriptor Port>
std::uint32_t Usart<Port>::DmaHead() noexcept {
  const auto size = static_cast<std::uint32_t>(Port::kRxBufferSize);
  const hal::cortex_m::InterruptLock lock;
  const std::uint32_t position = (size - Port::RxDma::Remaining()) % size;
  return rx_head_.load(std::memory_order_relaxed) +
         (position + size - rx_dma_position_) % size;
}

template <PortDescriptor Port>
std::size_t Usart<Port>::Available() noexcept {
  const std::uint32_t used = rx_head_.load(std::memory_order_acquire) -
                             rx_tail_.load(std::memory_order_relaxed);
  return std::min<std::size_t>(used, Port::kRxBufferSize);
}

template <PortDescriptor Port>
std::expected<void, Error> Usart<Port>::Write(
    std::span<const std::uint8_t> data) noexcept {
  if (data.empty()) {
    return {};
  }
  if (data.size() > kMaxWriteSize) {
    return std::unexpected(Error::kTooLarge);
  }
  if (!tx_ring_.TryWrite(data)) {
    return std::unexpected(Error::kNoSpace);
  }
  // A running batch picks the record up from its completion handler. The
  // record is committed before the flag is read (both sequentially
  // consistent), so either that handler sees it or this write pends the
  // drain itself.
  if (!tx_in_flight_.load()) {
    hal::cortex_m::SetPendingIrq<Traits::kIrq>();
  }
  return {};
}

template <PortDescriptor Port>
bool Usart<Port>::IsTxBusy() noexcept {
  return tx_in_flight_.load() || !tx_ring_.IsEmpty();
}

// Sends the committed records at the head of the TX queue in one transfer.
// Runs only at the port's interrupt priority (HandleIrq() and OnTxDma()), so
// it never preempts itself.
template <PortDescriptor Port>
void Usart<Port>::StartTx() noexcept {
  if (tx_paused_.load() || tx_in_flight_.load()) {
    return;
  }
  const auto batch = tx_ring_.Peek();
  if (!batch) {
    return;
  }
  tx_batch_ = *batch;
  tx_in_flight_.store(true);
  if (!Port::TxDma::Start(
          Traits::kBase + detail::kTdrOffset,
          reinterpret_cast<std::uintptr_t>(batch->bytes.data()),
          static_cast<std::uint16_t>(batch->bytes.size()))) {
    tx_ring_.Release(*batch);
    tx_in_flight_.store(false);
    callback_(Event::kTxDmaError);
  }
}

template <PortDescriptor Port>
void Usart<Port>::HandleIrq() noexcept {
  using P = detail::Paths<Traits::kBase>;

  const auto flags = groov::sync_read(P::kGroup(
      P::template kIsr<"idle">, P::template kIsr<"ore">,
      P::template kIsr<"nf">, P::template kIsr<"fe">,
      P::template kIsr<"pe">));
  const bool idle = flags[P::template kIsr<"idle">];
  const bool ore = flags[P::template kIsr<"ore">];
  const bool nf = flags[P::template kIsr<"nf">];
  const bool fe = flags[P::template kIsr<"fe">];
  const bool pe = flags[P::template kIsr<"pe">];

  // Clear exactly what was observed: one store, no read-modify-write.
  groov::sync_write(P::kGroup(
      P::template kIcr<"idlecf"> = idle, P::template kIcr<"orecf"> = ore,
      P::template kIcr<"ncf"> = nf, P::template kIcr<"fecf"> = fe,
      P::template kIcr<"pecf"> = pe));

  Event events = idle ? UpdateRxHead() : Event::kNone;
  if (ore) events |= Event::kOverrun;
  if (nf) events |= Event::kNoise;
  if (fe) events |= Event::kFramingError;
  if (pe) events |= Event::kParityError;
  if (Any(events)) {
    callback_(events);
  }
  // Write() pends this interrupt to start a transfer.
  StartTx();
}

// Advances rx_head_ to the DMA write position. Runs on IDLE, half-transfer
// and transfer-complete, so at most half a ring passes between two calls and
// the distance is never ambiguous.
template <PortDescriptor Port>
Event Usart<Port>::UpdateRxHead() noexcept {
  const auto size = static_cast<std::uint32_t>(Port::kRxBufferSize);
  const std::uint32_t position =
      (size - Port::RxDma::Remaining()) % size;
  const std::uint32_t delta = (position + size - rx_dma_position_) % size;
  if (delta == 0) {
    return Event::kNone;
  }
  rx_dma_position_ = position;

  const std::uint32_t head =
      rx_head_.load(std::memory_order_relaxed) + delta;
  rx_head_.store(head, std::memory_order_release);

  Event events = Event::kRxData;
  if (head - rx_tail_.load(std::memory_order_relaxed) > size) {
    events |= Event::kRxBufferOverflow;
  }
  return events;
}

template <PortDescriptor Port>
void Usart<Port>::OnRxDma(void* /*context*/, dma::Event events) noexcept {
  Event result = Event::kNone;
  if (dma::Any(events & (dma::Event::kHalfTransfer |
                         dma::Event::kTransferComplete))) {
    result |= UpdateRxHead();
  }
  if (dma::Any(events & dma::kErrorEvents)) {
    result |= Event::kRxDmaError;
  }
  if (Any(result)) {
    callback_(result);
  }
}

template <PortDescriptor Port>
void Usart<Port>::OnTxDma(void* /*context*/, dma::Event events) noexcept {
  // Also raised when Configure() stops a running transfer.
  if (!tx_in_flight_.load()) {
    return;
  }
  tx_ring_.Release(tx_batch_);
  tx_in_flight_.store(false);
  callback_(dma::Any(events & dma::kErrorEvents) ? Event::kTxDmaError
                                                 : Event::kTxComplete);
  StartTx();
}

}  // namespace uart

#endif  // STM32H7_DRIVERS_INCLUDE_UART_DETAIL_USART_IMPL_
