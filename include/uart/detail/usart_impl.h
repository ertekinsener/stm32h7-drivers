#ifndef APP_INCLUDE_UART_DETAIL_USART_IMPL_
#define APP_INCLUDE_UART_DETAIL_USART_IMPL_

// Definitions of uart::Usart members. Only the per-port translation units
// (app/source/uart/<port>.cc) include this file: instantiating groov code is
// expensive, so everything else uses the extern template declarations in
// uart/ports.h.

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <expected>
#include <utility>

#include "dma/stream.h"
#include "hal/gpio.h"
#include "stm32h7xx.h"
#include "uart/detail/baud.h"
#include "uart/usart.h"
#include "uart/usart_regs.h"

namespace uart {
namespace detail {

inline constexpr std::uint32_t kTxDrainTimeout = 1'000'000;
inline constexpr std::uintptr_t kRdrOffset = 0x24;
inline constexpr std::uintptr_t kTdrOffset = 0x28;

// groov paths of the USART registers used by the driver.
template <std::uintptr_t Base>
struct Paths {
  static constexpr auto kGroup = regs::usart<Base>;

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

  hal::ConfigureAlternate(Port::kTx, hal::Pull::kNone, hal::Speed::kMedium);
  // The pull-up keeps an unconnected RX line idle instead of floating.
  hal::ConfigureAlternate(Port::kRx, hal::Pull::kUp, hal::Speed::kMedium);
  if constexpr (Port::kRts.has_value()) {
    hal::ConfigureAlternate(*Port::kRts, hal::Pull::kNone, hal::Speed::kMedium);
  }
  if constexpr (Port::kCts.has_value()) {
    hal::ConfigureAlternate(*Port::kCts, hal::Pull::kNone, hal::Speed::kMedium);
  }

  // Same priority for the USART and both DMA streams: their handlers never
  // preempt each other, so UpdateRxHead() needs no lock.
  Port::RxDma::Init(Port::kIrqPriority);
  Port::TxDma::Init(Port::kIrqPriority);
  NVIC_SetPriority(Traits::kIrq,
                   NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                       Port::kIrqPriority, 0));
  NVIC_EnableIRQ(Traits::kIrq);
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

  // Stop both directions and let the last frame leave the shift register.
  if (!Port::TxDma::Stop() || !Port::RxDma::Stop()) {
    return std::unexpected(Error::kDma);
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
  tx_busy_.store(false, std::memory_order_relaxed);

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
  return {};
}

template <PortDescriptor Port>
std::size_t Usart<Port>::Read(std::span<std::uint8_t> data) noexcept {
  const std::span<std::uint8_t> ring = Port::RxBuffer();
  const auto size = static_cast<std::uint32_t>(ring.size());

  const std::uint32_t head = rx_head_.load(std::memory_order_acquire);
  std::uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  if (head - tail > size) {
    tail = head - size;  // Lapped: the oldest data was overwritten.
  }

  const auto count = static_cast<std::uint32_t>(
      std::min<std::size_t>(head - tail, data.size()));
  const std::uint32_t start = tail % size;
  const std::uint32_t first = std::min(count, size - start);
  std::memcpy(data.data(), ring.data() + start, first);
  std::memcpy(data.data() + first, ring.data(), count - first);

  rx_tail_.store(tail + count, std::memory_order_release);
  return count;
}

template <PortDescriptor Port>
std::size_t Usart<Port>::Available() noexcept {
  const std::uint32_t used = rx_head_.load(std::memory_order_acquire) -
                             rx_tail_.load(std::memory_order_relaxed);
  return std::min<std::size_t>(used, Port::kRxBufferSize);
}

template <PortDescriptor Port>
std::expected<std::size_t, Error> Usart<Port>::Write(
    std::span<const std::uint8_t> data) noexcept {
  if (data.empty()) {
    return 0;
  }
  bool idle = false;
  if (!tx_busy_.compare_exchange_strong(idle, true,
                                        std::memory_order_acquire)) {
    return std::unexpected(Error::kBusy);
  }

  const std::span<std::uint8_t> staging = Port::TxBuffer();
  const std::size_t count = std::min(data.size(), staging.size());
  std::memcpy(staging.data(), data.data(), count);

  if (!Port::TxDma::Start(Traits::kBase + detail::kTdrOffset,
                          reinterpret_cast<std::uintptr_t>(staging.data()),
                          static_cast<std::uint16_t>(count))) {
    tx_busy_.store(false, std::memory_order_release);
    return std::unexpected(Error::kDma);
  }
  return count;
}

template <PortDescriptor Port>
bool Usart<Port>::IsTxBusy() noexcept {
  return tx_busy_.load(std::memory_order_acquire);
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
  const Event result = dma::Any(events & dma::kErrorEvents)
                           ? Event::kTxDmaError
                           : Event::kTxComplete;
  tx_busy_.store(false, std::memory_order_release);
  callback_(result);
}

}  // namespace uart

#endif  // APP_INCLUDE_UART_DETAIL_USART_IMPL_
