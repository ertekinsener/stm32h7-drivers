// The EXTI interrupt of line 1 (group 1 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of that line is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[1].first == 1 && kLineGroups[1].last == 1,
              "EXTI1_IRQHandler serves lines 1..1");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<1>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI1_IRQHandler() {
  exti::detail::Dispatch<1>();
}
