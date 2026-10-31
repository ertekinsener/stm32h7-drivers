#ifndef STM32H7_DRIVERS_INCLUDE_DMA_STREAM_
#define STM32H7_DRIVERS_INCLUDE_DMA_STREAM_

#include <cstdint>
#include <expected>

#include "dma/buffer.h"
#include "dma/detail/stream_traits.h"
#include "dma/types.h"

namespace dma {

// One DMA1/DMA2 stream. The controller and stream number are template
// parameters so every register path resolves at compile time; there is one
// instantiation per hardware stream and all state is static.
//
// The member functions are defined in dma/detail/stream_impl.h and explicitly
// instantiated, together with the stream's IRQ handler, for each stream in the
// DMA_STREAMS CMake list (cmake/stm32h7_drivers/dma_streams.cmake), so only
// the streams in use are linked. Register access goes through the groov
// register layer (stm32h7::regs::dma), which is compiled optimized.
//
// A stream has a single owner and is not thread-safe. The callback runs in
// interrupt context; set it while the stream is idle.
//
//   using UartTx = dma::Stream<dma::Controller::kDma1, 1>;
//   UartTx::Init(5);
//   UartTx::SetCallback(completion.callback());
//   UartTx::Configure({.request = dma::Request::kUsart3Tx, ...});
//   UartTx::Start(&USART3->TDR, buffer.data(), buffer.size());
template <Controller C, std::uint8_t N>
class Stream {
 public:
  using Traits = detail::StreamTraits<C, N>;

  Stream() = delete;

  // Enables the controller clock and the stream interrupt in the NVIC.
  static void Init(std::uint32_t irq_priority) noexcept;

  static void SetCallback(Callback callback) noexcept { callback_ = callback; }

  // Stops the stream and programs DMAMUX, FIFO and control registers.
  static std::expected<void, Error> Configure(const Config& config) noexcept;

  // Starts a transfer of `count` items of the configured peripheral data
  // size. For memory-to-memory, `peripheral` is the source and `memory` the
  // destination. Both must be reachable by DMA (see DMA_BUFFER).
  [[gnu::flatten]] static std::expected<void, Error> Start(
      std::uintptr_t peripheral, std::uintptr_t memory,
      std::uint16_t count) noexcept;

  static std::expected<void, Error> Start(const volatile void* peripheral,
                                          const volatile void* memory,
                                          std::uint16_t count) noexcept {
    return Start(reinterpret_cast<std::uintptr_t>(peripheral),
                 reinterpret_cast<std::uintptr_t>(memory), count);
  }

  // Disables the stream, waits for the hardware to release it and clears
  // its flags.
  static std::expected<void, Error> Stop() noexcept;

  [[gnu::flatten]] static bool IsBusy() noexcept;

  // Items left to transfer (SxNDTR).
  [[gnu::flatten]] static std::uint16_t Remaining() noexcept;

  // Called from DMAx_StreamN_IRQHandler().
  [[gnu::flatten]] static void HandleIrq() noexcept;

 private:
  // flatten: groov's sync_read/sync_write wait stays out of line at -O2
  // otherwise; these run in interrupt context.
  [[gnu::flatten]] static void ClearFlags() noexcept;

  inline static Callback callback_{};
  inline static Event interrupts_ = Event::kNone;
};

extern template class Stream<Controller::kDma1, 0>;
extern template class Stream<Controller::kDma1, 1>;
extern template class Stream<Controller::kDma1, 2>;
extern template class Stream<Controller::kDma1, 3>;
extern template class Stream<Controller::kDma1, 4>;
extern template class Stream<Controller::kDma1, 5>;
extern template class Stream<Controller::kDma1, 6>;
extern template class Stream<Controller::kDma1, 7>;
extern template class Stream<Controller::kDma2, 0>;
extern template class Stream<Controller::kDma2, 1>;
extern template class Stream<Controller::kDma2, 2>;
extern template class Stream<Controller::kDma2, 3>;
extern template class Stream<Controller::kDma2, 4>;
extern template class Stream<Controller::kDma2, 5>;
extern template class Stream<Controller::kDma2, 6>;
extern template class Stream<Controller::kDma2, 7>;

}  // namespace dma

#endif  // STM32H7_DRIVERS_INCLUDE_DMA_STREAM_
