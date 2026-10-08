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
// mutex and readers by another; Configure() takes both. Driver events wake
// the waiting threads through an event flags group.
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

  // Sends all of `data`, in chunks of the port's TX staging buffer. Fails
  // with kTimeout if one chunk does not complete within `timeout` ticks.
  std::expected<std::size_t, Error> Write(std::span<const std::uint8_t> data,
                                          ULONG timeout) noexcept {
    if (tx_mutex_get(&tx_mutex_, timeout) != TX_SUCCESS) {
      return std::unexpected(Error::kTimeout);
    }
    std::size_t sent = 0;
    std::expected<std::size_t, Error> result = 0;
    while (sent < data.size()) {
      tx_event_flags_set(&flags_, ~kTxDoneFlag, TX_AND);
      tx_dma_error_.store(false);
      auto accepted = Driver::Write(data.subspan(sent));
      if (!accepted) {
        result = std::unexpected(accepted.error());
        break;
      }
      ULONG actual = 0;
      if (tx_event_flags_get(&flags_, kTxDoneFlag, TX_OR_CLEAR, &actual,
                             timeout) != TX_SUCCESS) {
        result = std::unexpected(Error::kTimeout);
        break;
      }
      if (tx_dma_error_.exchange(false)) {
        result = std::unexpected(Error::kDma);
        break;
      }
      sent += *accepted;
    }
    tx_mutex_put(&tx_mutex_);
    if (!result) {
      return result;
    }
    return sent;
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

 private:
  static constexpr ULONG kRxDataFlag = 1U << 0;
  static constexpr ULONG kTxDoneFlag = 1U << 1;

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
    if (Any(events & Event::kTxDmaError)) {
      // The aborted transfer also ends the writer's wait.
      self.tx_dma_error_.store(true);
    }
    if (Any(events & (Event::kTxComplete | Event::kTxDmaError))) {
      flags |= kTxDoneFlag;
    }
    if (flags != 0) {
      tx_event_flags_set(&self.flags_, flags, TX_OR);
    }
  }

  TX_MUTEX tx_mutex_{};
  TX_MUTEX rx_mutex_{};
  TX_EVENT_FLAGS_GROUP flags_{};
  std::atomic<std::uint16_t> errors_{0};
  std::atomic<bool> tx_dma_error_{false};
  bool created_ = false;
};

}  // namespace uart

#endif  // APP_INCLUDE_UART_THREADX_SERIAL_
