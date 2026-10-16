#ifndef STM32H7_DRIVERS_INCLUDE_HAL_RNG_
#define STM32H7_DRIVERS_INCLUDE_HAL_RNG_

#include <cstdint>
#include <span>

namespace hal::rng {

// The true random number generator (RNG, kernel clock HSI48, the reset
// selection). Plain CMSIS register access like hal/gpio.h.

// Starts HSI48 and the RNG. Call once before Fill()/Next(); false if HSI48
// does not start.
bool Init() noexcept;

// Fills `bytes` with random data. Busy-waits for the RNG (a few us per 32
// bits). False after a clock or seed error; the RNG is restarted for the
// next call. Thread-safe (each data register read is a short interrupt-masked
// section), not for ISRs.
bool Fill(std::span<std::uint8_t> bytes) noexcept;

// One random word, for users that cannot handle a failure (NetX Duo's
// NX_RAND: TCP initial sequence numbers, ephemeral ports, DHCP/DNS ids).
// After an RNG error it returns a xorshift value seeded from earlier RNG
// output and logs nothing. Thread-safe, not for ISRs.
std::uint32_t Next() noexcept;

}  // namespace hal::rng

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_RNG_
