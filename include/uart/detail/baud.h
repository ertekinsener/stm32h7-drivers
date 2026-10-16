#ifndef STM32H7_DRIVERS_INCLUDE_UART_DETAIL_BAUD_
#define STM32H7_DRIVERS_INCLUDE_UART_DETAIL_BAUD_

#include <cstdint>
#include <optional>

#include "uart/types.h"

namespace uart::detail {

// Largest accepted deviation of the real baud rate, in percent.
inline constexpr std::uint64_t kMaxBaudErrorPercent = 2;

// USART_BRR for a kernel clock (after PRESC = 1) and baud rate, or nullopt if
// the rate is out of range or too inaccurate (RM0399, "USART baud rate
// generation"):
//   OVER16: USARTDIV = f / baud,      BRR = USARTDIV
//   OVER8:  USARTDIV = 2 * f / baud,  BRR = USARTDIV[15:4] | USARTDIV[3:0] >> 1
// BRR must be >= 16.
constexpr std::optional<std::uint32_t> ComputeBrr(
    std::uint32_t kernel_clock_hz, std::uint32_t baud_rate,
    Oversampling oversampling) noexcept {
  if (baud_rate == 0) {
    return std::nullopt;
  }
  const std::uint64_t factor = oversampling == Oversampling::kBy8 ? 2 : 1;
  const std::uint64_t scaled_clock = factor * kernel_clock_hz;
  const std::uint64_t usartdiv = (scaled_clock + baud_rate / 2) / baud_rate;
  if (usartdiv < 16 || usartdiv > 0xFFFF) {
    return std::nullopt;
  }

  const std::uint64_t actual = scaled_clock / usartdiv;
  const std::uint64_t deviation =
      actual > baud_rate ? actual - baud_rate : baud_rate - actual;
  if (deviation * 100 > kMaxBaudErrorPercent * baud_rate) {
    return std::nullopt;
  }

  if (oversampling == Oversampling::kBy8) {
    return static_cast<std::uint32_t>((usartdiv & 0xFFF0U) |
                                      ((usartdiv & 0x000FU) >> 1));
  }
  return static_cast<std::uint32_t>(usartdiv);
}

}  // namespace uart::detail

#endif  // STM32H7_DRIVERS_INCLUDE_UART_DETAIL_BAUD_
