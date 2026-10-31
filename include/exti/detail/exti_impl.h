#ifndef STM32H7_DRIVERS_INCLUDE_EXTI_DETAIL_EXTI_IMPL_
#define STM32H7_DRIVERS_INCLUDE_EXTI_DETAIL_EXTI_IMPL_

// Definitions of exti::Line and exti::PinInterrupt, with groov on the
// generated maps (stm32h7::regs::exti, syscfg). Include only in sources
// built with -O2: the application's instances and the exti library.

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "exti/exti.h"
#include "hal/cortex_m.h"
#include "hal/detail/ct_name.h"
#include "stm32h7/irq.h"
#include "stm32h7/regs/exti.h"
#include "stm32h7/regs/rcc.h"
#include "stm32h7/regs/syscfg.h"

namespace exti {
namespace detail {

using hal::detail::Name;
using stm32h7::regs::exti::kExti;

// The registers of the core this code runs on.
#ifdef CORE_CM4
inline constexpr stdx::ct_string kImr = "c2imr1";
inline constexpr stdx::ct_string kEmr = "c2emr1";
inline constexpr stdx::ct_string kPr = "c2pr1";
#else
inline constexpr stdx::ct_string kImr = "c1imr1";
inline constexpr stdx::ct_string kEmr = "c1emr1";
inline constexpr stdx::ct_string kPr = "c1pr1";
#endif

// The NVIC interrupts of GPIO lines 0..15 (RM0399, NVIC vector table):
// lines 0..4 have one each, 5..9 and 10..15 share one. The only place that
// knows this grouping; the handler sources check their vector against it.
struct LineGroup {
  std::uint8_t first;
  std::uint8_t last;
  stm32h7::Irq irq;
};

inline constexpr std::array<LineGroup, 7> kLineGroups{{
    {0, 0, stm32h7::Irq::kExti0},
    {1, 1, stm32h7::Irq::kExti1},
    {2, 2, stm32h7::Irq::kExti2},
    {3, 3, stm32h7::Irq::kExti3},
    {4, 4, stm32h7::Irq::kExti4},
    {5, 9, stm32h7::Irq::kExti95},
    {10, 15, stm32h7::Irq::kExti1510},
}};

// Not constexpr: reaching it in GroupOf() is a compile error.
inline void NoLineGroup() noexcept {}

// The index in kLineGroups of the group of `line`.
consteval std::size_t GroupOf(std::uint8_t line) {
  for (std::size_t g = 0; g < kLineGroups.size(); ++g) {
    if (line >= kLineGroups[g].first && line <= kLineGroups[g].last) {
      return g;
    }
  }
  NoLineGroup();
  return 0;
}

// The handlers of the groups are each in a source of their own in the exti
// library (source/exti/exti*_irq.cc), so that only the handlers of the
// lines in use are linked. The startup code defines the vectors as weak, so
// a reference to a handler does not pull its archive member; each source
// defines the anchor LinkHandler<G>() as well, which
// PinInterrupt::Configure() refers to.
template <std::size_t G>
void LinkHandler() noexcept;
template <>
void LinkHandler<0>() noexcept;
template <>
void LinkHandler<1>() noexcept;
template <>
void LinkHandler<2>() noexcept;
template <>
void LinkHandler<3>() noexcept;
template <>
void LinkHandler<4>() noexcept;
template <>
void LinkHandler<5>() noexcept;
template <>
void LinkHandler<6>() noexcept;

// The callbacks of lines 0..15 (source/exti/exti.cc).
std::array<Callback, 16>& Callbacks() noexcept;

// Clears the pending lines of group G that are unmasked, with one store,
// then calls their callbacks. The body of the EXTI handlers.
template <std::size_t G>
[[gnu::flatten]] void Dispatch() noexcept {
  constexpr LineGroup kGroup = kLineGroups[G];
  constexpr std::uint32_t kLines =
      ((1U << (kGroup.last + 1U)) - 1U) & ~((1U << kGroup.first) - 1U);
  constexpr groov::path<kPr> kPrRegister{};
  constexpr groov::path<kImr> kImrRegister{};
  const std::uint32_t pending = groov::sync_read(kExti / kPrRegister) &
                                groov::sync_read(kExti / kImrRegister) & kLines;
  if (pending == 0) {
    return;
  }
  groov::sync_write(kExti(kPrRegister = pending));  // Write 1 to clear.
  for (std::uint32_t lines = pending; lines != 0; lines &= lines - 1U) {
    Callbacks()[static_cast<std::size_t>(std::countr_zero(lines))]();
  }
}

}  // namespace detail

// --- Line -------------------------------------------------------------------

template <std::uint8_t N>
void Line<N>::SetTrigger(Edge edge) noexcept {
  static_assert(N <= 21, "lines 0..21 have a configurable trigger");
  static constexpr auto kTr = detail::Name<"tr", N>();
  const auto mask = std::to_underlying(edge);
  groov::sync_write(detail::kExti(groov::path<"rtsr1", kTr>{} = (mask & 0b01U) != 0,
                                  groov::path<"ftsr1", kTr>{} = (mask & 0b10U) != 0));
}

template <std::uint8_t N>
void Line<N>::DisableTrigger() noexcept {
  static_assert(N <= 21, "lines 0..21 have a configurable trigger");
  static constexpr auto kTr = detail::Name<"tr", N>();
  groov::sync_write(detail::kExti(groov::path<"rtsr1", kTr>{} = false,
                                  groov::path<"ftsr1", kTr>{} = false));
}

template <std::uint8_t N>
void Line<N>::EnableInterrupt() noexcept {
  static constexpr auto kMr = detail::Name<"mr", N>();
  groov::sync_write(detail::kExti(groov::path<detail::kImr, kMr>{} = true));
}

template <std::uint8_t N>
void Line<N>::DisableInterrupt() noexcept {
  static constexpr auto kMr = detail::Name<"mr", N>();
  groov::sync_write(detail::kExti(groov::path<detail::kImr, kMr>{} = false));
}

template <std::uint8_t N>
bool Line<N>::IsInterruptEnabled() noexcept {
  static constexpr auto kMr = detail::Name<"mr", N>();
  return groov::sync_read(detail::kExti / groov::path<detail::kImr, kMr>{});
}

template <std::uint8_t N>
void Line<N>::EnableEvent() noexcept {
  static constexpr auto kMr = detail::Name<"mr", N>();
  groov::sync_write(detail::kExti(groov::path<detail::kEmr, kMr>{} = true));
}

template <std::uint8_t N>
void Line<N>::DisableEvent() noexcept {
  static constexpr auto kMr = detail::Name<"mr", N>();
  groov::sync_write(detail::kExti(groov::path<detail::kEmr, kMr>{} = false));
}

template <std::uint8_t N>
bool Line<N>::IsPending() noexcept {
  static_assert(N <= 21, "lines 0..21 have a pending flag");
  static constexpr auto kPrn = detail::Name<"pr", N>();
  return groov::sync_read(detail::kExti / groov::path<detail::kPr, kPrn>{});
}

// CnPR1 is write 1 to clear: groov writes 0 to the other flags, one store.
template <std::uint8_t N>
void Line<N>::ClearPending() noexcept {
  static_assert(N <= 21, "lines 0..21 have a pending flag");
  static constexpr auto kPrn = detail::Name<"pr", N>();
  groov::sync_write(detail::kExti(groov::path<detail::kPr, kPrn>{} = true));
}

template <std::uint8_t N>
void Line<N>::GenerateSoftwareInterrupt() noexcept {
  static_assert(N <= 21, "lines 0..21 have a software trigger");
  static constexpr auto kSwier = detail::Name<"swier", N>();
  groov::sync_write(detail::kExti(groov::path<"swier1", kSwier>{} = true));
}

// --- PinInterrupt -----------------------------------------------------------

template <gpio::PinId P>
void PinInterrupt<P>::Configure(Edge edge, Callback callback,
                                std::uint32_t irq_priority) noexcept {
  using stm32h7::regs::rcc::kRcc;
  using stm32h7::regs::syscfg::kSyscfg;
  constexpr std::uint8_t kLine = P.number;
  static constexpr auto kExticr = detail::Name<"exticr", kLine / 4U + 1U>();
  static constexpr auto kField = detail::Name<"exti", kLine>();

  constexpr groov::path<"apb4enr", "syscfgen"> kSyscfgen{};
  groov::sync_write(kRcc(kSyscfgen = true));
  static_cast<void>(groov::sync_read(kRcc / kSyscfgen));  // Clock running.

  Disable();
  groov::sync_write(kSyscfg(groov::path<kExticr, kField>{} =
                                std::to_underlying(P.port)));
  detail::SetCallback(kLine, callback);
  Line<kLine>::SetTrigger(edge);
  Line<kLine>::ClearPending();
  constexpr std::size_t kGroup = detail::GroupOf(kLine);
  constexpr stm32h7::Irq kIrq = detail::kLineGroups[kGroup].irq;
  // Links the group's handler (an archive member of its own).
  [[gnu::used]] static constexpr auto kAnchor = &detail::LinkHandler<kGroup>;
  hal::cortex_m::SetPriority<kIrq>(irq_priority);
  hal::cortex_m::EnableIrq<kIrq>();
  Enable();
}

template <gpio::PinId P>
void PinInterrupt<P>::Enable() noexcept {
  Line<P.number>::EnableInterrupt();
}

template <gpio::PinId P>
void PinInterrupt<P>::Disable() noexcept {
  Line<P.number>::DisableInterrupt();
}

template <gpio::PinId P>
void PinInterrupt<P>::SetCallback(Callback callback) noexcept {
  detail::SetCallback(P.number, callback);
}

}  // namespace exti

#endif  // STM32H7_DRIVERS_INCLUDE_EXTI_DETAIL_EXTI_IMPL_
