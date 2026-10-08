#ifndef APP_INCLUDE_UART_TYPES_
#define APP_INCLUDE_UART_TYPES_

#include <cstdint>
#include <utility>

namespace uart {

enum class Instance : std::uint8_t {
  kUsart1,
  kUsart2,
  kUsart3,
  kUart4,
  kUart5,
  kUsart6,
  kUart7,
  kUart8,
};

// Data bits, not counting the parity bit. 9 data bits would need 16-bit DMA
// transfers and are not supported by this byte-stream driver.
enum class WordLength : std::uint8_t { kBits7, kBits8 };

// Enumerator values are the CR2.STOP encodings. 0.5 stop bits exists only in
// smartcard mode.
enum class StopBits : std::uint8_t {
  kOne = 0b00,
  kTwo = 0b10,
  kOneAndHalf = 0b11,
};

enum class Parity : std::uint8_t { kNone, kEven, kOdd };

enum class FlowControl : std::uint8_t { kNone, kRts, kCts, kRtsCts };

enum class Oversampling : std::uint8_t { kBy16, kBy8 };

struct Config {
  std::uint32_t baud_rate = 115'200;
  WordLength data_bits = WordLength::kBits8;
  StopBits stop_bits = StopBits::kOne;
  Parity parity = Parity::kNone;
  FlowControl flow_control = FlowControl::kNone;
  Oversampling oversampling = Oversampling::kBy16;
  bool fifo = true;
  bool swap_tx_rx = false;
};

// Bitmask of events reported to the Callback (from interrupt context).
enum class Event : std::uint16_t {
  kNone = 0,
  kRxData = 1U << 0,
  kTxComplete = 1U << 1,
  kOverrun = 1U << 2,            // Hardware: RDR overwritten (ORE).
  kRxBufferOverflow = 1U << 3,   // Software: ring buffer lapped the reader.
  kFramingError = 1U << 4,
  kParityError = 1U << 5,
  kNoise = 1U << 6,
  kRxDmaError = 1U << 7,  // Reception stopped; Configure() restarts it.
  kTxDmaError = 1U << 8,  // The current Write() transfer was aborted.
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

inline constexpr Event kLineErrors = Event::kOverrun | Event::kFramingError |
                                     Event::kParityError | Event::kNoise;

enum class Error : std::uint8_t {
  kInvalidConfig,
  kBusy,
  kTimeout,
  kDma,
  kRtos,
};

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

}  // namespace uart

#endif  // APP_INCLUDE_UART_TYPES_
