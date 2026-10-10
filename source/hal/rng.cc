#include "hal/rng.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "stm32h7xx.h"

namespace hal::rng {
namespace {

// Polls before giving up; far longer than HSI48 start-up (a few us) or one
// RNG word (RM0399: 42 RNG clock cycles once seeded).
constexpr std::uint32_t kTimeoutPolls = 1'000'000;

bool WaitFor(volatile std::uint32_t& reg, std::uint32_t mask) noexcept {
  for (std::uint32_t i = 0; i < kTimeoutPolls; ++i) {
    if ((reg & mask) != 0) {
      return true;
    }
  }
  return false;
}

void Restart() noexcept {
  CLEAR_BIT(RNG->CR, RNG_CR_RNGEN);
  WRITE_REG(RNG->SR, 0);  // Clears CEIS and SEIS.
  SET_BIT(RNG->CR, RNG_CR_RNGEN);
}

}  // namespace

bool Init() noexcept {
  SET_BIT(RCC->CR, RCC_CR_HSI48ON);
  if (!WaitFor(RCC->CR, RCC_CR_HSI48RDY)) {
    return false;
  }
  CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_RNGSEL);  // HSI48.
  SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
  static_cast<void>(READ_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN));
  SET_BIT(RNG->CR, RNG_CR_RNGEN);
  return true;
}

bool Fill(std::span<std::uint8_t> bytes) noexcept {
  while (!bytes.empty()) {
    if (!WaitFor(RNG->SR, RNG_SR_DRDY | RNG_SR_SEIS | RNG_SR_CEIS) ||
        READ_BIT(RNG->SR, RNG_SR_SEIS | RNG_SR_CEIS) != 0) {
      Restart();
      return false;
    }
    std::uint32_t word = READ_REG(RNG->DR);
    // DR reads 0 when a seed error happened meanwhile (RM0399).
    if (word == 0 && READ_BIT(RNG->SR, RNG_SR_SEIS) != 0) {
      Restart();
      return false;
    }
    const std::size_t take = bytes.size() < 4 ? bytes.size() : 4;
    for (std::size_t i = 0; i < take; ++i) {
      bytes[i] = static_cast<std::uint8_t>(word);
      word >>= 8;
    }
    bytes = bytes.subspan(take);
  }
  return true;
}

}  // namespace hal::rng
