#ifndef STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_IMPL_
#define STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_IMPL_

// Definitions of dma::Stream members, with groov on the generated register
// maps (stm32h7::regs::dma). Only the generated per-stream translation units
// (cmake/stm32h7_drivers/stream_instance.cc.in) include this file; they are
// built with -O2, where groov collapses into plain loads and stores.
// Everything else uses the extern template declarations in dma/stream.h, so
// only the streams in use are linked.

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstdint>
#include <expected>
#include <utility>

#include "dma/dmamux.h"
#include "dma/stream.h"
#include "hal/cortex_m.h"
#include "stm32h7/regs/rcc.h"

namespace dma {
namespace detail {

inline constexpr std::uint32_t kStopTimeout = 100'000;

// groov paths of one stream's registers and flag fields.
template <Controller C, std::uint8_t N>
struct Paths {
  using Traits = StreamTraits<C, N>;

  template <stdx::ct_string Field>
  static constexpr groov::path<Traits::kCr, Field> kCr{};
  template <stdx::ct_string Field>
  static constexpr groov::path<Traits::kFcr, Field> kFcr{};
  static constexpr groov::path<Traits::kPar, "pa"> kPar{};
  static constexpr groov::path<Traits::kM0ar, "m0a"> kM0ar{};
  static constexpr groov::path<Traits::kNdtr, "ndt"> kNdtr{};

  static constexpr groov::path<Traits::kFlagReg, Traits::kTcif> kTcif{};
  static constexpr groov::path<Traits::kFlagReg, Traits::kHtif> kHtif{};
  static constexpr groov::path<Traits::kFlagReg, Traits::kTeif> kTeif{};
  static constexpr groov::path<Traits::kFlagReg, Traits::kDmeif> kDmeif{};
  static constexpr groov::path<Traits::kFlagReg, Traits::kFeif> kFeif{};

  static constexpr groov::path<Traits::kClearReg, Traits::kCtcif> kCtcif{};
  static constexpr groov::path<Traits::kClearReg, Traits::kChtif> kChtif{};
  static constexpr groov::path<Traits::kClearReg, Traits::kCteif> kCteif{};
  static constexpr groov::path<Traits::kClearReg, Traits::kCdmeif> kCdmeif{};
  static constexpr groov::path<Traits::kClearReg, Traits::kCfeif> kCfeif{};
};

constexpr bool IsValid(const Config& config) noexcept {
  const bool fifo_mode = config.fifo != Fifo::kDirect;
  if (config.direction == Direction::kMemoryToMemory) {
    // RM0399: circular and direct modes are not allowed for memory-to-memory.
    return fifo_mode && config.mode == Mode::kNormal;
  }
  // In direct mode the hardware forces MSIZE = PSIZE.
  return fifo_mode || config.memory_size == config.peripheral_size;
}

}  // namespace detail

template <Controller C, std::uint8_t N>
void Stream<C, N>::Init(std::uint32_t irq_priority) noexcept {
  constexpr groov::path<"ahb1enr", Traits::kClockEnable> kEnable{};
  groov::sync_write(stm32h7::regs::rcc::kRcc(kEnable = true));
  // Read back so the clock is running before the first register access.
  static_cast<void>(groov::sync_read(stm32h7::regs::rcc::kRcc / kEnable));
  hal::cortex_m::SetPriority<Traits::kIrq>(irq_priority);
  hal::cortex_m::EnableIrq<Traits::kIrq>();
}

template <Controller C, std::uint8_t N>
std::expected<void, Error> Stream<C, N>::Configure(
    const Config& config) noexcept {
  using P = detail::Paths<C, N>;

  if (!detail::IsValid(config)) {
    return std::unexpected(Error::kInvalidConfig);
  }
  if (auto stopped = Stop(); !stopped) {
    return stopped;
  }

  dmamux1::Route<Traits::kMuxChannel>(config.request);

  const bool fifo_mode = config.fifo != Fifo::kDirect;
  const auto threshold =
      fifo_mode ? std::to_underlying(config.fifo) - 1U : 1U;
  groov::sync_write(Traits::kGroup(
      P::template kFcr<"feie"> =
          fifo_mode && Any(config.interrupts & Event::kFifoError),
      P::template kFcr<"dmdis"> = fifo_mode,
      P::template kFcr<"fth"> = threshold));

  const Event irqs = config.interrupts;
  groov::sync_write(Traits::kGroup(
      P::template kCr<"mburst"> = 0U, P::template kCr<"pburst"> = 0U,
      P::template kCr<"trbuff"> = config.bufferable,
      P::template kCr<"ct"> = false, P::template kCr<"dbm"> = false,
      P::template kCr<"pl"> = std::to_underlying(config.priority),
      P::template kCr<"pincos"> = false,
      P::template kCr<"msize"> = std::to_underlying(config.memory_size),
      P::template kCr<"psize"> = std::to_underlying(config.peripheral_size),
      P::template kCr<"minc"> = config.memory_increment,
      P::template kCr<"pinc"> = config.peripheral_increment,
      P::template kCr<"circ"> = config.mode == Mode::kCircular,
      P::template kCr<"dir"> = std::to_underlying(config.direction),
      P::template kCr<"pfctrl"> = false,
      P::template kCr<"tcie"> = Any(irqs & Event::kTransferComplete),
      P::template kCr<"htie"> = Any(irqs & Event::kHalfTransfer),
      P::template kCr<"teie"> = Any(irqs & Event::kTransferError),
      P::template kCr<"dmeie"> = Any(irqs & Event::kDirectModeError),
      P::template kCr<"en"> = false));

  interrupts_ = irqs;
  return {};
}

template <Controller C, std::uint8_t N>
std::expected<void, Error> Stream<C, N>::Start(std::uintptr_t peripheral,
                                               std::uintptr_t memory,
                                               std::uint16_t count) noexcept {
  using P = detail::Paths<C, N>;

  if (count == 0) {
    return std::unexpected(Error::kInvalidLength);
  }
  if (!IsDmaReachable(peripheral) || !IsDmaReachable(memory)) {
    return std::unexpected(Error::kInvalidAddress);
  }
  if (IsBusy()) {
    return std::unexpected(Error::kBusy);
  }

  ClearFlags();
  groov::sync_write(Traits::kGroup(P::kPar = peripheral, P::kM0ar = memory,
                                   P::kNdtr = count));
  groov::sync_write(Traits::kGroup(P::template kCr<"en"> = true));
  return {};
}

template <Controller C, std::uint8_t N>
std::expected<void, Error> Stream<C, N>::Stop() noexcept {
  using P = detail::Paths<C, N>;

  groov::sync_write(Traits::kGroup(P::template kCr<"en"> = false));
  for (std::uint32_t i = 0; IsBusy(); ++i) {
    if (i == detail::kStopTimeout) {
      return std::unexpected(Error::kTimeout);
    }
  }
  ClearFlags();
  return {};
}

template <Controller C, std::uint8_t N>
bool Stream<C, N>::IsBusy() noexcept {
  using P = detail::Paths<C, N>;
  return groov::sync_read(Traits::kGroup / P::template kCr<"en">);
}

template <Controller C, std::uint8_t N>
std::uint16_t Stream<C, N>::Remaining() noexcept {
  using P = detail::Paths<C, N>;
  return static_cast<std::uint16_t>(
      groov::sync_read(Traits::kGroup / P::kNdtr));
}

template <Controller C, std::uint8_t N>
void Stream<C, N>::HandleIrq() noexcept {
  using P = detail::Paths<C, N>;

  const auto flags = groov::sync_read(
      Traits::kGroup(P::kTcif, P::kHtif, P::kTeif, P::kDmeif, P::kFeif));
  const bool tc = flags[P::kTcif];
  const bool ht = flags[P::kHtif];
  const bool te = flags[P::kTeif];
  const bool dme = flags[P::kDmeif];
  const bool fe = flags[P::kFeif];

  // Clear exactly what was observed: one store, no read-modify-write.
  groov::sync_write(Traits::kGroup(P::kCtcif = tc, P::kChtif = ht,
                                   P::kCteif = te, P::kCdmeif = dme,
                                   P::kCfeif = fe));

  Event events = Event::kNone;
  if (tc) events |= Event::kTransferComplete;
  if (ht) events |= Event::kHalfTransfer;
  if (te) events |= Event::kTransferError;
  if (dme) events |= Event::kDirectModeError;
  if (fe) events |= Event::kFifoError;
  // FEIF may be set without FEIE (e.g. in direct mode); report only the
  // events that were asked for.
  events = events & interrupts_;

  if (Any(events & (Event::kTransferError | Event::kDirectModeError))) {
    groov::sync_write(Traits::kGroup(P::template kCr<"en"> = false));
  }
  if (Any(events)) {
    callback_(events);
  }
}

template <Controller C, std::uint8_t N>
void Stream<C, N>::ClearFlags() noexcept {
  using P = detail::Paths<C, N>;
  groov::sync_write(Traits::kGroup(P::kCtcif = true, P::kChtif = true,
                                   P::kCteif = true, P::kCdmeif = true,
                                   P::kCfeif = true));
}

}  // namespace dma

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_DETAIL_STREAM_IMPL_
