#ifndef APP_INCLUDE_DMA_TYPES_
#define APP_INCLUDE_DMA_TYPES_

#include <cstdint>
#include <utility>

namespace dma {

enum class Controller : std::uint8_t { kDma1 = 0, kDma2 = 1 };

// Enumerator values are the hardware encodings of the SxCR/SxFCR fields.
enum class Direction : std::uint8_t {
  kPeripheralToMemory = 0b00,
  kMemoryToPeripheral = 0b01,
  // PAR is the source and M0AR the destination. Not allowed in circular mode.
  kMemoryToMemory = 0b10,
};

enum class DataSize : std::uint8_t {
  kByte = 0b00,
  kHalfWord = 0b01,
  kWord = 0b10,
};

enum class Priority : std::uint8_t {
  kLow = 0b00,
  kMedium = 0b01,
  kHigh = 0b10,
  kVeryHigh = 0b11,
};

enum class Mode : std::uint8_t { kNormal, kCircular };

// kDirect disables the FIFO. The others enable it with the given threshold.
enum class Fifo : std::uint8_t {
  kDirect,
  kQuarter,
  kHalf,
  kThreeQuarters,
  kFull,
};

// DMAMUX1 request lines (RM0399, "DMAMUX1: assignment of multiplexer inputs
// to resources"). Extend as peripherals are added.
enum class Request : std::uint8_t {
  kMemoryToMemory = 0,
  kSpi1Rx = 37,
  kSpi1Tx = 38,
  kUsart1Rx = 41,
  kUsart1Tx = 42,
  kUsart2Rx = 43,
  kUsart2Tx = 44,
  kUsart3Rx = 45,
  kUsart3Tx = 46,
  kUart4Rx = 63,
  kUart4Tx = 64,
  kUart5Rx = 65,
  kUart5Tx = 66,
  kUsart6Rx = 71,
  kUsart6Tx = 72,
  kUart7Rx = 79,
  kUart7Tx = 80,
  kUart8Rx = 81,
  kUart8Tx = 82,
};

// Bitmask of stream events. Also selects which interrupts Configure() enables.
enum class Event : std::uint8_t {
  kNone = 0,
  kTransferComplete = 1U << 0,
  kHalfTransfer = 1U << 1,
  kTransferError = 1U << 2,
  kDirectModeError = 1U << 3,
  kFifoError = 1U << 4,
};

constexpr Event operator|(Event lhs, Event rhs) noexcept {
  return static_cast<Event>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr Event operator&(Event lhs, Event rhs) noexcept {
  return static_cast<Event>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

constexpr Event& operator|=(Event& lhs, Event rhs) noexcept {
  return lhs = lhs | rhs;
}

constexpr bool Any(Event events) noexcept { return events != Event::kNone; }

inline constexpr Event kErrorEvents =
    Event::kTransferError | Event::kDirectModeError | Event::kFifoError;

enum class Error : std::uint8_t {
  kBusy,
  kInvalidConfig,
  kInvalidAddress,
  kInvalidLength,
  kTimeout,
  kTransferError,
  kRtos,
};

struct Config {
  Request request = Request::kMemoryToMemory;
  Direction direction = Direction::kPeripheralToMemory;
  Mode mode = Mode::kNormal;
  DataSize peripheral_size = DataSize::kByte;
  DataSize memory_size = DataSize::kByte;
  bool peripheral_increment = false;
  bool memory_increment = true;
  Priority priority = Priority::kLow;
  Fifo fifo = Fifo::kDirect;
  // Must be true when the stream serves a U(S)ART (SxCR.TRBUFF, RM0399).
  bool bufferable = false;
  Event interrupts = Event::kTransferComplete | Event::kTransferError |
                     Event::kDirectModeError | Event::kFifoError;
};

// Called from interrupt context with the events that occurred.
struct Callback {
  using Function = void (*)(void* context, Event events) noexcept;

  Function function = nullptr;
  void* context = nullptr;

  void operator()(Event events) const noexcept {
    if (function != nullptr) {
      function(context, events);
    }
  }
};

}  // namespace dma

#endif  // APP_INCLUDE_DMA_TYPES_
