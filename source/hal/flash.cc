#include "hal/flash.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <groov/groov.hpp>

#include "hal/cortex_m.h"
#include "stm32h7/regs/flash.h"

namespace hal::flash {
namespace {

using stm32h7::regs::flash::kFlash;

constexpr std::uint32_t kKey1 = 0x45670123;
constexpr std::uint32_t kKey2 = 0xCDEF89AB;

// 32-bit parallelism: valid at any supply voltage (RM0399, FLASH_CR).
constexpr std::uint8_t kPsizeX32 = 0b10;

template <stdx::ct_string Field>
constexpr groov::path<"cr2", Field> kCr2{};
template <stdx::ct_string Field>
constexpr groov::path<"sr2", Field> kSr2{};
template <stdx::ct_string Field>
constexpr groov::path<"ccr2", Field> kCcr2{};

bool IsBusy() noexcept {
  const auto sr = groov::sync_read(
      kFlash(kSr2<"bsy2">, kSr2<"qw2">, kSr2<"wbne2">));
  return sr[kSr2<"bsy2">] || sr[kSr2<"qw2">] || sr[kSr2<"wbne2">];
}

bool HasError() noexcept {
  const auto sr = groov::sync_read(kFlash(
      kSr2<"wrperr2">, kSr2<"pgserr2">, kSr2<"strberr2">, kSr2<"incerr2">,
      kSr2<"operr2">, kSr2<"rdperr2">, kSr2<"rdserr2">, kSr2<"sneccerr2">,
      kSr2<"dbeccerr2">));
  return sr[kSr2<"wrperr2">] || sr[kSr2<"pgserr2">] || sr[kSr2<"strberr2">] ||
         sr[kSr2<"incerr2">] || sr[kSr2<"operr2">] || sr[kSr2<"rdperr2">] ||
         sr[kSr2<"rdserr2">] || sr[kSr2<"sneccerr2">] || sr[kSr2<"dbeccerr2">];
}

// Write 1 to clear: one store.
void ClearEcc() noexcept {
  groov::sync_write(kFlash(kCcr2<"clr_sneccerr2"> = true,
                           kCcr2<"clr_dbeccerr2"> = true));
}

void ClearAll() noexcept {
  groov::sync_write(kFlash(
      kCcr2<"clr_eop2"> = true, kCcr2<"clr_wrperr2"> = true,
      kCcr2<"clr_pgserr2"> = true, kCcr2<"clr_strberr2"> = true,
      kCcr2<"clr_incerr2"> = true, kCcr2<"clr_operr2"> = true,
      kCcr2<"clr_rdperr2"> = true, kCcr2<"clr_rdserr2"> = true,
      kCcr2<"clr_sneccerr2"> = true, kCcr2<"clr_dbeccerr2"> = true));
}

// Polls until bank 2 is idle: a flash word takes tens of microseconds, a
// sector erase up to 4 s (DS12919: t_ERASE128KB). With `yield`, it is
// called between polls (a 1 ms tick: 8 s), else the loop spins.
constexpr std::uint32_t kMaxYields = 8'000;
constexpr std::uint32_t kMaxSpins = 50'000'000;

bool WaitIdle(Yield yield) noexcept {
  const std::uint32_t limit = yield != nullptr ? kMaxYields : kMaxSpins;
  for (std::uint32_t i = 0; i < limit; ++i) {
    if (!IsBusy()) {
      return true;
    }
    if (yield != nullptr) {
      yield();
    }
  }
  return false;
}

void Unlock() noexcept {
  if (groov::sync_read(kFlash / kCr2<"lock2">)) {
    groov::sync_write(kFlash(groov::path<"keyr2", "keyr2">{} = kKey1));
    groov::sync_write(kFlash(groov::path<"keyr2", "keyr2">{} = kKey2));
  }
}

void Lock() noexcept { groov::sync_write(kFlash(kCr2<"lock2"> = true)); }

bool InBank2(std::uintptr_t address, std::size_t size) noexcept {
  return address >= kBank2Base &&
         address + size <= kBank2Base + kSectorsPerBank * kSectorSize;
}

// Ends an operation: locks the bank and reports (then clears) its errors.
bool Finish(bool idle) noexcept {
  const bool ok = idle && !HasError();
  ClearAll();
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
  ClearAll();
  groov::sync_write(kFlash(kCr2<"psize2"> = kPsizeX32,
                           kCr2<"snb2"> = static_cast<std::uint8_t>(sector),
                           kCr2<"ser2"> = true));
  groov::sync_write(kFlash(kCr2<"start2"> = true));
  const bool idle = WaitIdle(yield);
  groov::sync_write(kFlash(kCr2<"ser2"> = false, kCr2<"snb2"> = 0U));
  // The D-cache may still hold the old contents.
  cortex_m::InvalidateDCache(Bank2SectorAddress(sector), kSectorSize);
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
  ClearAll();
  groov::sync_write(kFlash(kCr2<"psize2"> = kPsizeX32, kCr2<"pg2"> = true));
  // Eight 32-bit stores fill the write buffer; the programming starts once
  // it holds the whole flash word.
  volatile std::uint32_t* const target =
      reinterpret_cast<volatile std::uint32_t*>(address);
  for (std::size_t i = 0; i < kFlashWord / 4; ++i) {
    target[i] = words[i];
  }
  cortex_m::Isb();
  cortex_m::Dsb();
  const bool idle = WaitIdle(nullptr);
  groov::sync_write(kFlash(kCr2<"pg2"> = false));
  cortex_m::InvalidateDCache(address, kFlashWord);
  return Finish(idle);
}

bool ReadBank2(std::uintptr_t address, std::span<std::uint8_t> out) noexcept {
  if (!InBank2(address, out.size())) {
    return false;
  }
  ClearEcc();
  // With FAULTMASK set, BFHFNMIGN makes the core ignore the bus error of a
  // load from a flash word with a double ECC error (the data is garbage
  // then, and DBECCERR tells).
  const std::uint32_t faultmask = cortex_m::GetFaultmask();
  cortex_m::SetFaultmask(1);
  cortex_m::SetBusFaultIgnore(true);
  cortex_m::Dsb();
  cortex_m::Isb();
  const volatile std::uint8_t* const source =
      reinterpret_cast<const volatile std::uint8_t*>(address);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = source[i];
  }
  cortex_m::Dsb();
  const bool ecc_error = groov::sync_read(kFlash / kSr2<"dbeccerr2">);
  cortex_m::SetBusFaultIgnore(false);
  cortex_m::Dsb();
  cortex_m::Isb();
  cortex_m::SetFaultmask(faultmask);
  if (ecc_error) {
    ClearEcc();
    // A cached copy of the bad word must not hide the error next time.
    cortex_m::InvalidateDCache(address, out.size());
  }
  return !ecc_error;
}

}  // namespace hal::flash
