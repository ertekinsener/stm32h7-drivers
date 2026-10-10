#ifndef APP_INCLUDE_UART_THREADX_SERIAL_
#define APP_INCLUDE_UART_THREADX_SERIAL_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "tx_api.h"
#include "uart/types.h"
#include "uart/usart.h"

namespace uart {

// Blocking ThreadX front end for Usart<Port>. Writers are serialized by one
// mutex, so a Write() larger than the driver's kMaxWriteSize still comes out
// in one piece, and readers by another; Configure() takes both. Driver events
// wake the waiting threads through an event flags group.
//
// Usart<Port>::Write() stays available for code that must not block (e.g. a
// logger called from interrupts); its records interleave with the chunks of
// a Write() here, never split them.
//
//   uart::ThreadxSerial<uart::Usart3Vcp> serial;
//   serial.Create("vcp");
//   serial.Configure({.baud_rate = 115'200});
//   serial.Write(std::as_bytes(...), TX_WAIT_FOREVER);
template <PortDescriptor Port>
class ThreadxSerial {
 public:
  using Driver = Usart<Port>;

  ThreadxSerial() = default;
  ThreadxSerial(const ThreadxSerial&) = delete;
  ThreadxSerial& operator=(const ThreadxSerial&) = delete;
  ~ThreadxSerial() {
    if (created_) {
      tx_event_flags_delete(&flags_);
      tx_mutex_delete(&rx_mutex_);
      tx_mutex_delete(&tx_mutex_);
    }
  }

  // Creates the RTOS objects and initializes the port hardware. The port is
  // not usable until Configure() succeeds.
  std::expected<void, Error> Create(const char* name) noexcept {
    if (created_) {
      return {};
    }
    // ThreadX only stores the name pointer and never writes through it.
    auto* const tx_name = const_cast<CHAR*>(name);
    if (tx_mutex_create(&tx_mutex_, tx_name, TX_INHERIT) != TX_SUCCESS ||
        tx_mutex_create(&rx_mutex_, tx_name, TX_INHERIT) != TX_SUCCESS ||
        tx_event_flags_create(&flags_, tx_name) != TX_SUCCESS) {
      return std::unexpected(Error::kRtos);
    }
    created_ = true;
    Driver::Init();
    Driver::SetCallback({&OnEvent, this});
    return {};
  }

  // Waits for a running Write() and Read() to finish, then reconfigures.
  std::expected<void, Error> Configure(const Config& config) noexcept {
    tx_mutex_get(&tx_mutex_, TX_WAIT_FOREVER);
    tx_mutex_get(&rx_mutex_, TX_WAIT_FOREVER);
    auto result = Driver::Configure(config);
    tx_mutex_put(&rx_mutex_);
    tx_mutex_put(&tx_mutex_);
    return result;
  }

  // Queues all of `data` in chunks of at most Driver::kMaxWriteSize bytes and
  // returns once it is queued, not sent. While the queue is full, waits for
  // a batch to complete; fails with kTimeout if one wait exceeds `timeout`
  // ticks, after the chunks queued so far. TX DMA errors are reported
  // through TakeErrors().
  std::expected<std::size_t, Error> Write(std::span<const std::uint8_t> data,
                                          ULONG timeout) noexcept {
    if (tx_mutex_get(&tx_mutex_, timeout) != TX_SUCCESS) {
      return std::unexpected(Error::kTimeout);
    }
    std::size_t queued = 0;
    std::expected<std::size_t, Error> result = 0;
    while (queued < data.size()) {
      const auto chunk = data.subspan(
          queued, std::min(data.size() - queued, Driver::kMaxWriteSize));
      // Cleared before trying, so space freed after a failed attempt still
      // ends the wait below.
      tx_event_flags_set(&flags_, ~kTxSpaceFlag, TX_AND);
      const auto written = Driver::Write(chunk);
      if (written) {
        queued += chunk.size();
        continue;
      }
      if (written.error() != Error::kNoSpace) {
        result = std::unexpected(written.error());
        break;
      }
      ULONG actual = 0;
      if (tx_event_flags_get(&flags_, kTxSpaceFlag, TX_OR_CLEAR, &actual,
                             timeout) != TX_SUCCESS) {
        result = std::unexpected(Error::kTimeout);
        break;
      }
    }
    tx_mutex_put(&tx_mutex_);
    if (!result) {
      return result;
    }
    return queued;
  }

  // Returns as soon as at least one byte is available, up to data.size().
  // Fails with kTimeout if nothing arrives within `timeout` ticks.
  std::expected<std::size_t, Error> Read(std::span<std::uint8_t> data,
                                         ULONG timeout) noexcept {
    if (data.empty()) {
      return 0;
    }
    if (tx_mutex_get(&rx_mutex_, timeout) != TX_SUCCESS) {
      return std::unexpected(Error::kTimeout);
    }
    std::expected<std::size_t, Error> result = 0;
    while (true) {
      if (const std::size_t count = Driver::Read(data); count > 0) {
        result = count;
        break;
      }
      ULONG actual = 0;
      if (tx_event_flags_get(&flags_, kRxDataFlag, TX_OR_CLEAR, &actual,
                             timeout) != TX_SUCCESS) {
        result = std::unexpected(Error::kTimeout);
        break;
      }
    }
    tx_mutex_put(&rx_mutex_);
    return result;
  }

  // Line, overflow and DMA errors seen since the last call.
  Event TakeErrors() noexcept {
    return static_cast<Event>(errors_.exchange(0));
  }

  // Also passes every driver event to `listener` (interrupt context), e.g.
  // to wake a thread that serves several ports. `listener` must outlive the
  // port; nullptr removes it.
  void SetListener(const Callback* listener) noexcept {
    listener_.store(listener, std::memory_order_release);
  }

 private:
  static constexpr ULONG kRxDataFlag = 1U << 0;
  static constexpr ULONG kTxSpaceFlag = 1U << 1;

  static void OnEvent(void* context, Event events) noexcept {
    auto& self = *static_cast<ThreadxSerial*>(context);
    const Event errors = events & (kLineErrors | Event::kRxBufferOverflow |
                                   Event::kRxDmaError | Event::kTxDmaError);
    if (Any(errors)) {
      self.errors_.fetch_or(std::to_underlying(errors));
    }
    ULONG flags = 0;
    if (Any(events & Event::kRxData)) {
      flags |= kRxDataFlag;
    }
    // A batch freed its queue space, whether it was sent or dropped.
    if (Any(events & (Event::kTxComplete | Event::kTxDmaError))) {
      flags |= kTxSpaceFlag;
    }
    if (flags != 0) {
      tx_event_flags_set(&self.flags_, flags, TX_OR);
    }
    if (const Callback* listener =
            self.listener_.load(std::memory_order_acquire)) {
      (*listener)(events);
    }
  }

  TX_MUTEX tx_mutex_{};
  TX_MUTEX rx_mutex_{};
  TX_EVENT_FLAGS_GROUP flags_{};
  std::atomic<std::uint16_t> errors_{0};
  std::atomic<const Callback*> listener_{nullptr};
  bool created_ = false;
};

}  // namespace uart

#endif  // APP_INCLUDE_UART_THREADX_SERIAL_
