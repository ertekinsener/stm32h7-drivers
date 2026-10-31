#ifndef STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_CT_NAME_
#define STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_CT_NAME_

#include <stdx/ct_string.hpp>

#include <cstddef>

namespace hal::detail {

// Decimal representation of Value as a compile-time string, e.g. 12 -> "12",
// for groov paths of numbered registers and fields ("s3cr", "mode8").
template <std::size_t Value>
consteval auto ToCtString() {
  static_assert(Value < 1000);
  constexpr std::size_t kDigits = Value < 10 ? 1 : Value < 100 ? 2 : 3;
  char digits[kDigits]{};
  std::size_t value = Value;
  for (std::size_t i = kDigits; i-- > 0;) {
    digits[i] = static_cast<char>('0' + value % 10);
    value /= 10;
  }
  return stdx::ct_string<kDigits + 1>{digits, kDigits};
}

// Prefix + Value + suffix, e.g. Name<"s", 3, "cr">() -> "s3cr".
template <stdx::ct_string Prefix, std::size_t Value, stdx::ct_string Suffix = "">
consteval auto Name() {
  return Prefix + ToCtString<Value>() + Suffix;
}

}  // namespace hal::detail

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_CT_NAME_
