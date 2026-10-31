// The EXTI interrupt of line 3 (group 3 of exti::detail::kLineGroups),
// linked only when a PinInterrupt of that line is configured.

#include "exti/detail/exti_impl.h"

namespace exti::detail {

static_assert(kLineGroups[3].first == 3 && kLineGroups[3].last == 3,
              "EXTI3_IRQHandler serves lines 3..3");

// Referred to by PinInterrupt::Configure(): pulls this file from the library.
template <>
void LinkHandler<3>() noexcept {}

}  // namespace exti::detail

extern "C" void EXTI3_IRQHandler() {
  exti::detail::Dispatch<3>();
}
