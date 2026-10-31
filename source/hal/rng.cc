#include "hal/rng.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include <groov/groov.hpp>

#include "hal/cortex_m_cpu.h"
#include "stm32h7/regs/rcc.h"
#include "stm32h7/regs/rng.h"

namespace hal::rng {
namespace {

using cortex_m::InterruptLock;
using stm32h7::regs::rcc::kRcc;
using stm32h7::regs::rng::kRng;

constexpr groov::path<"sr", "drdy"> kDrdy{};
constexpr groov::path<"sr", "ceis"> kCeis{};
constexpr groov::path<"sr", "seis"> kSeis{};
constexpr groov::path<"cr", "rngen"> kRngen{};

// Polls before giving up; far longer than HSI48 start-up (a few us) or one
// RNG word (RM0399: 42 RNG clock cycles once seeded).
constexpr std::uint32_t kTimeoutPolls = 1'000'000;

template <typename Predicate>
bool WaitFor(Predicate done) noexcept {
  for (std::uint32_t i = 0; i < kTimeoutPolls; ++i) {
    if (done()) {
      return true;
    }
  }
  return false;
}

// Set by Init(); without it ReadWord() fails at once instead of timing out.
bool started = false;

// Fallback generator state for Next(), mixed with every good RNG word.
// Only accessed under InterruptLock.
std::uint32_t fallback_state = 0x9E3779B9u;

void Restart() noexcept {
  const InterruptLock lock;
  groov::sync_write(kRng(kRngen = false));
  // CEIS/SEIS are cleared by writing 0 (groov writes 1 to the other bits).
  groov::sync_write(kRng(kCeis = false, kSeis = false));
  groov::sync_write(kRng(kRngen = true));
}

// Reads one RNG word. Waits for DRDY with interrupts enabled; DRDY is
// checked again and DR read under the lock, so two threads never take the
// same word (the loser waits for the next one). False after a clock or seed
// error, with the RNG restarted.
bool ReadWord(std::uint32_t& word) noexcept {
  if (!started) {
    return false;
  }
  for (std::uint32_t i = 0; i < kTimeoutPolls; ++i) {
    bool error = false;
    if (!WaitFor([&] {
          const auto sr = groov::sync_read(kRng(kDrdy, kCeis, kSeis));
          error = sr[kCeis] || sr[kSeis];
          return sr[kDrdy] || error;
        }) ||
        error) {
      break;
    }
    const InterruptLock lock;
    if (!groov::sync_read(kRng / kDrdy)) {
      continue;  // Another thread took it.
    }
    word = groov::sync_read(kRng / groov::path<"dr", "rndata">{});
    // DR reads 0 when a seed error happened meanwhile (RM0399).
    if (word == 0 && groov::sync_read(kRng / kSeis)) {
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
  groov::sync_write(kRcc(groov::path<"cr", "hsi48on">{} = true));
  if (!WaitFor(
          [] { return groov::sync_read(kRcc / groov::path<"cr", "hsi48rdy">{}); })) {
    return false;
  }
  groov::sync_write(kRcc(groov::path<"d2ccip2r", "rngsel">{} = 0U));  // HSI48.
  groov::sync_write(kRcc(groov::path<"ahb2enr", "rngen">{} = true));
  // Read back: the clock runs before the first RNG access.
  static_cast<void>(groov::sync_read(kRcc / groov::path<"ahb2enr", "rngen">{}));
  groov::sync_write(kRng(kRngen = true));
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
