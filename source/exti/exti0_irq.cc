// The EXTI interrupt of line 0 (group 0 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of that line is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[0].first == 0 && kLineGroups[0].last == 0,
              "EXTI0_IRQHandler serves lines 0..0");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<0>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI0_IRQHandler() {
  exti::detail::Dispatch<0>();
}
