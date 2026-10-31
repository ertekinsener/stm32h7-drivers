// The EXTI interrupt of lines 10..15 (group 6 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of one of them is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[6].first == 10 && kLineGroups[6].last == 15,
              "EXTI15_10_IRQHandler serves lines 10..15");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<6>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI15_10_IRQHandler() {
  exti::detail::Dispatch<6>();
}
