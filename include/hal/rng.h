#ifndef APP_INCLUDE_HAL_RNG_
#define APP_INCLUDE_HAL_RNG_

#include <cstdint>
#include <span>

namespace hal::rng {

// The true random number generator (RNG, kernel clock HSI48, the reset
// selection). Plain CMSIS register access like hal/gpio.h.

// Starts HSI48 and the RNG. Call once before Fill(); false if HSI48 does not
// start.
bool Init() noexcept;

// Fills `bytes` with random data. Busy-waits for the RNG (a few us per 32
// bits). False after a clock or seed error; the RNG is restarted for the
// next call. Not reentrant: call from one thread only.
bool Fill(std::span<std::uint8_t> bytes) noexcept;

}  // namespace hal::rng

#endif  // APP_INCLUDE_HAL_RNG_
