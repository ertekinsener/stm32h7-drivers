#ifndef STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_
#define STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_

// Compile-time pin configuration with groov (named fields: MODER.MODE8, ...).
// Driver-internal: for the driver sources built with -O2.

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstdint>
#include <utility>

#include "hal/detail/ct_name.h"
#include "hal/gpio.h"
#include "stm32h7/regs/gpio.h"
#include "stm32h7/regs/rcc.h"

namespace hal {
namespace detail {

// GPIOA..GPIOK are 0x400 apart on AHB4 (RM0399, memory map).
inline constexpr std::uintptr_t kPortStride =
    stm32h7::regs::gpio::kGpiobBase - stm32h7::regs::gpio::kGpioaBase;
static_assert(stm32h7::regs::gpio::kGpiokBase ==
              stm32h7::regs::gpio::kGpioaBase + 10 * kPortStride);

// The groov map of a port.
template <Port P>
inline constexpr auto kPortMap = stm32h7::regs::gpio::kMap<
    stm32h7::regs::gpio::kGpioaBase + std::to_underlying(P) * kPortStride>;

// "gpioaen" .. "gpioken", the port's clock enable in RCC_AHB4ENR.
template <Port P>
consteval auto PortClockEnable() {
  const char letter[1]{static_cast<char>('a' + std::to_underlying(P))};
  return stdx::ct_string{"gpio"} + stdx::ct_string<2>{letter, 1} +
         stdx::ct_string{"en"};
}

// Enables the port clock and reads it back, so it runs before the first
// access to the port.
template <Port P>
void EnableClock() noexcept {
  static constexpr auto kField = PortClockEnable<P>();
  constexpr groov::path<"ahb4enr", kField> kEnable{};
  groov::sync_write(stm32h7::regs::rcc::kRcc(kEnable = true));
  static_cast<void>(groov::sync_read(stm32h7::regs::rcc::kRcc / kEnable));
}

}  // namespace detail

// Enables the port clock and puts the pin in push-pull alternate-function
// mode. Alternate function, speed, type and pull are set before MODER, so
// the pin never drives another function.
template <Pin P>
void ConfigureAlternate(Pull pull, Speed speed) noexcept {
  static_assert(P.number < 16 && P.alternate < 16);
  constexpr auto kPort = detail::kPortMap<P.port>;
  static constexpr stdx::ct_string<5> kAfr = P.number < 8 ? "afrl" : "afrh";
  static constexpr auto kAfsel = detail::Name<"afsel", P.number>();
  static constexpr auto kOspeed = detail::Name<"ospeed", P.number>();
  static constexpr auto kOt = detail::Name<"ot", P.number>();
  static constexpr auto kPupd = detail::Name<"pupd", P.number>();
  static constexpr auto kMode = detail::Name<"mode", P.number>();

  detail::EnableClock<P.port>();
  groov::sync_write(kPort(groov::path<kAfr, kAfsel>{} = P.alternate));
  groov::sync_write(kPort(groov::path<"ospeedr", kOspeed>{} =
                              std::to_underlying(speed)));
  groov::sync_write(kPort(groov::path<"otyper", kOt>{} = false));  // Push-pull.
  groov::sync_write(kPort(groov::path<"pupdr", kPupd>{} = std::to_underlying(pull)));
  groov::sync_write(kPort(groov::path<"moder", kMode>{} = 0b10U));  // Alternate.
}

}  // namespace hal

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_DETAIL_GPIO_IMPL_
