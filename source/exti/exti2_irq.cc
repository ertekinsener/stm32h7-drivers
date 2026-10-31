// The EXTI interrupt of line 2 (group 2 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of that line is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[2].first == 2 && kLineGroups[2].last == 2,
              "EXTI2_IRQHandler serves lines 2..2");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<2>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI2_IRQHandler() {
  exti::detail::Dispatch<2>();
}
