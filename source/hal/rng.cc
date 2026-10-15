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

// Masks interrupts and restores PRIMASK on exit (nestable).
class InterruptLock {
 public:
  InterruptLock() noexcept : primask_(__get_PRIMASK()) { __disable_irq(); }
  InterruptLock(const InterruptLock&) = delete;
  InterruptLock& operator=(const InterruptLock&) = delete;
  ~InterruptLock() { __set_PRIMASK(primask_); }

 private:
  std::uint32_t primask_;
};

// Set by Init(); without it ReadWord() fails at once instead of timing out.
bool started = false;

// Fallback generator state for Next(), mixed with every good RNG word.
// Only accessed under InterruptLock.
std::uint32_t fallback_state = 0x9E3779B9u;

void Restart() noexcept {
  const InterruptLock lock;
  CLEAR_BIT(RNG->CR, RNG_CR_RNGEN);
  WRITE_REG(RNG->SR, 0);  // Clears CEIS and SEIS.
  SET_BIT(RNG->CR, RNG_CR_RNGEN);
}

// Reads one RNG word. Waits for DRDY with interrupts enabled; DRDY is
// checked again and DR read under the lock, so two threads never take the
// same word (the loser waits for the next one). False after a clock or seed
// error, with the RNG restarted.
bool ReadWord(std::uint32_t& word) noexcept {
  constexpr std::uint32_t kErrors = RNG_SR_SEIS | RNG_SR_CEIS;
  if (!started) {
    return false;
  }
  for (std::uint32_t i = 0; i < kTimeoutPolls; ++i) {
    if (!WaitFor(RNG->SR, RNG_SR_DRDY | kErrors) ||
        READ_BIT(RNG->SR, kErrors) != 0) {
      break;
    }
    const InterruptLock lock;
    if (READ_BIT(RNG->SR, RNG_SR_DRDY) == 0) {
      continue;  // Another thread took it.
    }
    word = READ_REG(RNG->DR);
    // DR reads 0 when a seed error happened meanwhile (RM0399).
    if (word == 0 && READ_BIT(RNG->SR, RNG_SR_SEIS) != 0) {
      break;
    }
    fallback_state ^= word;
    return true;
  }
  Restart();
  return false;
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
  started = true;
  return true;
}

bool Fill(std::span<std::uint8_t> bytes) noexcept {
  while (!bytes.empty()) {
    std::uint32_t word = 0;
    if (!ReadWord(word)) {
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

std::uint32_t Next() noexcept {
  std::uint32_t word = 0;
  if (ReadWord(word)) {
    return word;
  }
  // xorshift32; a zero state (an RNG word cancelled it) would stick at 0.
  const InterruptLock lock;
  std::uint32_t x = fallback_state != 0 ? fallback_state : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  fallback_state = x;
  return x;
}

}  // namespace hal::rng
