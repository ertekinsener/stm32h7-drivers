// The EXTI interrupt of lines 5..9 (group 5 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of one of them is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[5].first == 5 && kLineGroups[5].last == 9,
              "EXTI9_5_IRQHandler serves lines 5..9");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<5>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI9_5_IRQHandler() {
  exti::detail::Dispatch<5>();
}
