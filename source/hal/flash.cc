#include "hal/flash.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "stm32h7xx.h"

namespace hal::flash {
namespace {

constexpr std::uint32_t kKey1 = 0x45670123;
constexpr std::uint32_t kKey2 = 0xCDEF89AB;

constexpr std::uint32_t kBusy = FLASH_SR_BSY | FLASH_SR_QW | FLASH_SR_WBNE;
constexpr std::uint32_t kErrors =
    FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR |
    FLASH_SR_OPERR | FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_SNECCERR |
    FLASH_SR_DBECCERR;
constexpr std::uint32_t kClearAll =
    FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR |
    FLASH_CCR_CLR_STRBERR | FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR |
    FLASH_CCR_CLR_RDPERR | FLASH_CCR_CLR_RDSERR | FLASH_CCR_CLR_SNECCERR |
    FLASH_CCR_CLR_DBECCERR;
// 32-bit parallelism: valid at any supply voltage (RM0399, FLASH_CR).
constexpr std::uint32_t kPsizeX32 = FLASH_CR_PSIZE_1;

// Polls until bank 2 is idle: a flash word takes tens of microseconds, a
// sector erase up to 4 s (DS12919: t_ERASE128KB). With `yield`, it is
// called between polls (a 1 ms tick: 8 s), else the loop spins.
constexpr std::uint32_t kMaxYields = 8'000;
constexpr std::uint32_t kMaxSpins = 50'000'000;

bool WaitIdle(Yield yield) noexcept {
  const std::uint32_t limit = yield != nullptr ? kMaxYields : kMaxSpins;
  for (std::uint32_t i = 0; i < limit; ++i) {
    if (READ_BIT(FLASH->SR2, kBusy) == 0) {
      return true;
    }
    if (yield != nullptr) {
      yield();
    }
  }
  return false;
}

void Unlock() noexcept {
  if (READ_BIT(FLASH->CR2, FLASH_CR_LOCK) != 0) {
    WRITE_REG(FLASH->KEYR2, kKey1);
    WRITE_REG(FLASH->KEYR2, kKey2);
  }
}

void Lock() noexcept { SET_BIT(FLASH->CR2, FLASH_CR_LOCK); }

bool InBank2(std::uintptr_t address, std::size_t size) noexcept {
  return address >= kBank2Base &&
         address + size <= kBank2Base + kSectorsPerBank * kSectorSize;
}

// Ends an operation: locks the bank and reports (then clears) its errors.
bool Finish(bool idle) noexcept {
  const bool ok = idle && READ_BIT(FLASH->SR2, kErrors) == 0;
  WRITE_REG(FLASH->CCR2, kClearAll);
  Lock();
  return ok;
}

}  // namespace

bool EraseBank2Sector(std::size_t sector, Yield yield) noexcept {
  if (sector >= kSectorsPerBank) {
    return false;
  }
  Unlock();
  if (!WaitIdle(nullptr)) {
    return Finish(false);
  }
  WRITE_REG(FLASH->CCR2, kClearAll);
  MODIFY_REG(FLASH->CR2, FLASH_CR_PSIZE | FLASH_CR_SNB,
             kPsizeX32 | FLASH_CR_SER |
                 (static_cast<std::uint32_t>(sector) << FLASH_CR_SNB_Pos));
  SET_BIT(FLASH->CR2, FLASH_CR_START);
  const bool idle = WaitIdle(yield);
  CLEAR_BIT(FLASH->CR2, FLASH_CR_SER | FLASH_CR_SNB);
  // The D-cache may still hold the old contents.
  SCB_InvalidateDCache_by_Addr(
      reinterpret_cast<void*>(Bank2SectorAddress(sector)),
      static_cast<std::int32_t>(kSectorSize));
  return Finish(idle);
}

bool ProgramBank2(std::uintptr_t address,
                  std::span<const std::uint8_t, kFlashWord> data) noexcept {
  if (address % kFlashWord != 0 || !InBank2(address, kFlashWord)) {
    return false;
  }
  std::uint32_t words[kFlashWord / 4];
  std::memcpy(words, data.data(), sizeof(words));

  Unlock();
  if (!WaitIdle(nullptr)) {
    return Finish(false);
  }
  WRITE_REG(FLASH->CCR2, kClearAll);
  MODIFY_REG(FLASH->CR2, FLASH_CR_PSIZE, kPsizeX32 | FLASH_CR_PG);
  // Eight 32-bit stores fill the write buffer; the programming starts once
  // it holds the whole flash word.
  volatile std::uint32_t* const target =
      reinterpret_cast<volatile std::uint32_t*>(address);
  for (std::size_t i = 0; i < kFlashWord / 4; ++i) {
    target[i] = words[i];
  }
  __ISB();
  __DSB();
  const bool idle = WaitIdle(nullptr);
  CLEAR_BIT(FLASH->CR2, FLASH_CR_PG);
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<void*>(address),
                               static_cast<std::int32_t>(kFlashWord));
  return Finish(idle);
}

bool ReadBank2(std::uintptr_t address, std::span<std::uint8_t> out) noexcept {
  if (!InBank2(address, out.size())) {
    return false;
  }
  WRITE_REG(FLASH->CCR2, FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_SNECCERR);
  // With FAULTMASK set, BFHFNMIGN makes the core ignore the bus error of a
  // load from a flash word with a double ECC error (the data is garbage
  // then, and DBECCERR tells).
  const std::uint32_t faultmask = __get_FAULTMASK();
  __set_FAULTMASK(1);
  SCB->CCR |= SCB_CCR_BFHFNMIGN_Msk;
  __DSB();
  __ISB();
  const volatile std::uint8_t* const source =
      reinterpret_cast<const volatile std::uint8_t*>(address);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = source[i];
  }
  __DSB();
  const bool ecc_error = READ_BIT(FLASH->SR2, FLASH_SR_DBECCERR) != 0;
  SCB->CCR &= ~SCB_CCR_BFHFNMIGN_Msk;
  __DSB();
  __ISB();
  __set_FAULTMASK(faultmask);
  if (ecc_error) {
    WRITE_REG(FLASH->CCR2, FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_SNECCERR);
    // A cached copy of the bad word must not hide the error next time.
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<void*>(address),
                                 static_cast<std::int32_t>(out.size()));
  }
  return !ecc_error;
}

}  // namespace hal::flash
