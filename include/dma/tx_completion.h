#ifndef STM32H7_DRIVERS_INCLUDE_DMA_TX_COMPLETION_
#define STM32H7_DRIVERS_INCLUDE_DMA_TX_COMPLETION_

#include <expected>

#include "dma/types.h"
#include "tx_api.h"

namespace dma {

// Lets a ThreadX thread block until a stream reports an event. The driver
// core stays RTOS-agnostic; this adapter only provides a Callback that sets
// event flags from the ISR.
//
//   dma::TxCompletion done;
//   done.Create("uart tx dma");
//   UartTx::SetCallback(done.callback());
//   UartTx::Start(...);
//   auto events = done.Wait(TX_WAIT_FOREVER);
class TxCompletion {
 public:
  TxCompletion() = default;
  TxCompletion(const TxCompletion&) = delete;
  TxCompletion& operator=(const TxCompletion&) = delete;
  ~TxCompletion();

  std::expected<void, Error> Create(const char* name) noexcept;

  Callback callback() noexcept { return {&OnEvent, this}; }

  // Waits for any stream event and consumes it. Fails with kTransferError if
  // an error event arrived, kTimeout if nothing arrived in `timeout_ticks`.
  std::expected<Event, Error> Wait(ULONG timeout_ticks) noexcept;

 private:
  static void OnEvent(void* context, Event events) noexcept;

  TX_EVENT_FLAGS_GROUP flags_{};
  bool created_ = false;
};

}  // namespace dma

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_TX_COMPLETION_
