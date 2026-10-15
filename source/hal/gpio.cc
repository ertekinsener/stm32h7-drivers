#include "hal/gpio.h"

#include <cstdint>
#include <utility>

#include "stm32h7xx.h"

namespace hal {
namespace {

constexpr std::uint32_t kModeInput = 0b00;
constexpr std::uint32_t kModeAlternate = 0b10;

GPIO_TypeDef* Registers(Port port) noexcept {
  // GPIOA..GPIOK are 0x400 apart on AHB4 (RM0399, memory map).
  return reinterpret_cast<GPIO_TypeDef*>(
      GPIOA_BASE + std::to_underlying(port) * (GPIOB_BASE - GPIOA_BASE));
}

void EnableClock(Port port) noexcept {
  const std::uint32_t port_bit = RCC_AHB4ENR_GPIOAEN << std::to_underlying(port);
  SET_BIT(RCC->AHB4ENR, port_bit);
  static_cast<void>(READ_BIT(RCC->AHB4ENR, port_bit));
}

}  // namespace

void ConfigureAlternate(Pin pin, Pull pull, Speed speed) noexcept {
  EnableClock(pin.port);

  GPIO_TypeDef* const gpio = Registers(pin.port);
  const std::uint32_t shift2 = pin.number * 2U;
  const std::uint32_t shift4 = (pin.number % 8U) * 4U;

  MODIFY_REG(gpio->AFR[pin.number / 8U], 0xFU << shift4,
             static_cast<std::uint32_t>(pin.alternate) << shift4);
  MODIFY_REG(gpio->OSPEEDR, 0b11U << shift2,
             static_cast<std::uint32_t>(std::to_underlying(speed)) << shift2);
  CLEAR_BIT(gpio->OTYPER, 1U << pin.number);
  MODIFY_REG(gpio->PUPDR, 0b11U << shift2,
             static_cast<std::uint32_t>(std::to_underlying(pull)) << shift2);
  MODIFY_REG(gpio->MODER, 0b11U << shift2, kModeAlternate << shift2);
}

void ConfigureInput(Pin pin, Pull pull) noexcept {
  EnableClock(pin.port);

  GPIO_TypeDef* const gpio = Registers(pin.port);
  const std::uint32_t shift2 = pin.number * 2U;
  MODIFY_REG(gpio->PUPDR, 0b11U << shift2,
             static_cast<std::uint32_t>(std::to_underlying(pull)) << shift2);
  MODIFY_REG(gpio->MODER, 0b11U << shift2, kModeInput << shift2);
}

bool Read(Pin pin) noexcept {
  return READ_BIT(Registers(pin.port)->IDR, 1U << pin.number) != 0;
}

}  // namespace hal
