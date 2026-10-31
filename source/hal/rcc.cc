#include "hal/rcc.h"

#include <groov/groov.hpp>

#include <array>
#include <cstdint>

#include "stm32h7/regs/rcc.h"

// CMSIS system file of the application (system_stm32h7xx.c).
extern "C" std::uint32_t SystemCoreClock;

namespace hal::rcc {
namespace {

using stm32h7::regs::rcc::kRcc;

// Right shifts of the AHB prescaler (HPRE, D1CPRE: 0xxx = /1, 1000 = /2 ...
// 1111 = /512) and of the APB prescalers (D2PPREx: 0xx = /1, 100 = /2 ...
// 111 = /16), RM0399 RCC_D1CFGR and RCC_D2CFGR.
constexpr std::array<std::uint8_t, 16> kAhbShift{0, 0, 0, 0, 0, 0, 0, 0,
                                                 1, 2, 3, 4, 6, 7, 8, 9};
constexpr std::array<std::uint8_t, 8> kApbShift{0, 0, 0, 0, 1, 2, 3, 4};

}  // namespace

std::uint32_t HclkHz() noexcept {
#ifdef CORE_CM4
  return SystemCoreClock;
#else
  const std::uint8_t hpre =
      groov::sync_read(kRcc / groov::path<"d1cfgr", "hpre">{});
  return SystemCoreClock >> kAhbShift[hpre];
#endif
}

std::uint32_t Pclk1Hz() noexcept {
  const std::uint8_t ppre =
      groov::sync_read(kRcc / groov::path<"d2cfgr", "d2ppre1">{});
  return HclkHz() >> kApbShift[ppre];
}

std::uint32_t Pclk2Hz() noexcept {
  const std::uint8_t ppre =
      groov::sync_read(kRcc / groov::path<"d2cfgr", "d2ppre2">{});
  return HclkHz() >> kApbShift[ppre];
}

}  // namespace hal::rcc
