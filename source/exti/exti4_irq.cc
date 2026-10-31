// The EXTI interrupt of line 4 (group 4 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of that line is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[4].first == 4 && kLineGroups[4].last == 4,
              "EXTI4_IRQHandler serves lines 4..4");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<4>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI4_IRQHandler() {
  exti::detail::Dispatch<4>();
}
