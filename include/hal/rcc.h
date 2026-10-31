#ifndef STM32H7_DRIVERS_INCLUDE_HAL_RCC_
#define STM32H7_DRIVERS_INCLUDE_HAL_RCC_

#include <cstdint>

namespace hal::rcc {

// Bus clock frequencies. The clock tree itself (PLLs, prescalers) belongs to
// the application; peripheral clocks are enabled by the drivers through the
// register layer (stm32h7::regs::rcc), reading the enable register back so
// the clock runs before the first access to the peripheral (RM0399, RCC).
//
// From the CMSIS variable SystemCoreClock and the RCC prescalers.
// SystemCoreClock is the core clock: on the CM7 sys_ck / D1CPRE (HCLK = that
// / HPRE), on the CM4 (CORE_CM4 defined) HCLK. The application keeps it
// current, as ST's HAL_RCC_ClockConfig() and SystemCoreClockUpdate() do.
std::uint32_t HclkHz() noexcept;
std::uint32_t Pclk1Hz() noexcept;  // D2 APB1 (rcc_pclk1).
std::uint32_t Pclk2Hz() noexcept;  // D2 APB2 (rcc_pclk2).

}  // namespace hal::rcc

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_RCC_
