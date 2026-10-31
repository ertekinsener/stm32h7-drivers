#include "exti/exti.h"

#include <array>
#include <cstdint>

#include "exti/detail/exti_impl.h"

namespace exti::detail {
namespace {

// Written by PinInterrupt::Configure()/SetCallback() while the line is
// masked, read by the handlers.
std::array<Callback, 16> callbacks{};

}  // namespace

std::array<Callback, 16>& Callbacks() noexcept { return callbacks; }

void SetCallback(std::uint8_t line, Callback callback) noexcept {
  callbacks[line] = callback;
}

}  // namespace exti::detail
