#ifndef STM32H7_DRIVERS_INCLUDE_HAL_FLASH_
#define STM32H7_DRIVERS_INCLUDE_HAL_FLASH_

#include <cstddef>
#include <cstdint>
#include <span>

namespace hal::flash {

// Erase, program and ECC-safe reads of internal flash bank 2 (0x08100000,
// eight 128 KiB sectors), for data the CM7 keeps there (e.g. settings). On
// the dual-core STM32H7 the CM7 runs from bank 1, so it keeps running while
// bank 2 is busy; a CM4 running from bank 2 stalls meanwhile.
// Plain CMSIS register access like hal/gpio.h. Not thread safe: one user.

inline constexpr std::uintptr_t kBank2Base = 0x08100000;
inline constexpr std::size_t kSectorSize = 128 * 1024;
inline constexpr std::size_t kSectorsPerBank = 8;
// Programming unit: 256 bits with ECC, written once between erases.
inline constexpr std::size_t kFlashWord = 32;

constexpr std::uintptr_t Bank2SectorAddress(std::size_t sector) noexcept {
  return kBank2Base + sector * kSectorSize;
}

// Called while an erase is in progress (1-2 s); e.g. sleeps a tick so
// other threads run. nullptr busy-waits.
using Yield = void (*)() noexcept;

// Erases `sector` (0..7) of bank 2.
bool EraseBank2Sector(std::size_t sector, Yield yield) noexcept;

// Programs one flash word at `address` (bank 2, kFlashWord-aligned, erased).
bool ProgramBank2(std::uintptr_t address,
                  std::span<const std::uint8_t, kFlashWord> data) noexcept;

// Copies `out.size()` bytes from bank 2. A flash word with a double ECC
// error (cut short by a power loss) makes it return false instead of
// raising a bus fault: the read runs with FAULTMASK and BFHFNMIGN set.
bool ReadBank2(std::uintptr_t address, std::span<std::uint8_t> out) noexcept;

}  // namespace hal::flash

#endif  // STM32H7_DRIVERS_INCLUDE_HAL_FLASH_
