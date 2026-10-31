#ifndef STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_
#define STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstdint>
#include <utility>

#include "dma/types.h"
#include "hal/detail/ct_name.h"
#include "hal/rcc.h"
#include "stm32h7/irq.h"
#include "stm32h7/regs/rcc.h"
#include "stm32h7/regs/usart.h"
#include "uart/types.h"

namespace uart::detail {

// Kernel clock domain of an instance: USART1/6 run from D2 PCLK2, all others
// from D2 PCLK1. The driver selects PCLK as kernel clock (D2CCIP2R = 0).
enum class ClockDomain : std::uint8_t { kApb1, kApb2 };

struct InstanceInfo {
  std::uintptr_t base;
  stm32h7::Irq irq;
  dma::Request rx_request;
  dma::Request tx_request;
  ClockDomain domain;
  // USARTn or UARTn: the name of the bus clock enable field ("usart3en").
  bool usart;
  std::uint8_t number;
};

consteval InstanceInfo Describe(Instance instance) {
  namespace r = stm32h7::regs::usart;
  using enum stm32h7::Irq;
  using enum ClockDomain;
  switch (instance) {
    case Instance::kUsart1:
      return {r::kUsart1Base, kUsart1, dma::Request::kUsart1Rx,
              dma::Request::kUsart1Tx, kApb2, true, 1};
    case Instance::kUsart2:
      return {r::kUsart2Base, kUsart2, dma::Request::kUsart2Rx,
              dma::Request::kUsart2Tx, kApb1, true, 2};
    case Instance::kUsart3:
      return {r::kUsart3Base, kUsart3, dma::Request::kUsart3Rx,
              dma::Request::kUsart3Tx, kApb1, true, 3};
    case Instance::kUart4:
      return {r::kUart4Base, kUart4, dma::Request::kUart4Rx,
              dma::Request::kUart4Tx, kApb1, false, 4};
    case Instance::kUart5:
      return {r::kUart5Base, kUart5, dma::Request::kUart5Rx,
              dma::Request::kUart5Tx, kApb1, false, 5};
    case Instance::kUsart6:
      return {r::kUsart6Base, kUsart6, dma::Request::kUsart6Rx,
              dma::Request::kUsart6Tx, kApb2, true, 6};
    case Instance::kUart7:
      return {r::kUart7Base, kUart7, dma::Request::kUart7Rx,
              dma::Request::kUart7Tx, kApb1, false, 7};
    case Instance::kUart8:
      return {r::kUart8Base, kUart8, dma::Request::kUart8Rx,
              dma::Request::kUart8Tx, kApb1, false, 8};
  }
  std::unreachable();
}

// The bus clock enable field of an instance, from its table entry:
// "usart1en" .. "uart8en", in RCC_APB2ENR (kApb2) or RCC_APB1LENR.
template <Instance I>
consteval auto ClockEnableField() {
  constexpr InstanceInfo kInfo = Describe(I);
  if constexpr (kInfo.usart) {
    return hal::detail::Name<"usart", kInfo.number, "en">();
  } else {
    return hal::detail::Name<"uart", kInfo.number, "en">();
  }
}

template <Instance I>
struct InstanceTraits {
  static constexpr InstanceInfo kInfo = Describe(I);
  static constexpr std::uintptr_t kBase = kInfo.base;
  static constexpr stm32h7::Irq kIrq = kInfo.irq;
  static constexpr dma::Request kRxRequest = kInfo.rx_request;
  static constexpr dma::Request kTxRequest = kInfo.tx_request;

  // Selects PCLK as kernel clock and enables the bus clock, reading it back
  // so the clock runs before the first register access.
  static void EnableClock() noexcept {
    using stm32h7::regs::rcc::kRcc;
    static constexpr auto kField = ClockEnableField<I>();
    if constexpr (kInfo.domain == ClockDomain::kApb2) {
      constexpr groov::path<"apb2enr", kField> kEnable{};
      groov::sync_write(kRcc(groov::path<"d2ccip2r", "usart16sel">{} = 0U));
      groov::sync_write(kRcc(kEnable = true));
      static_cast<void>(groov::sync_read(kRcc / kEnable));
    } else {
      constexpr groov::path<"apb1lenr", kField> kEnable{};
      groov::sync_write(kRcc(groov::path<"d2ccip2r", "usart234578sel">{} = 0U));
      groov::sync_write(kRcc(kEnable = true));
      static_cast<void>(groov::sync_read(kRcc / kEnable));
    }
  }

  static std::uint32_t KernelClockHz() noexcept {
    if constexpr (kInfo.domain == ClockDomain::kApb2) {
      return hal::rcc::Pclk2Hz();
    } else {
      return hal::rcc::Pclk1Hz();
    }
  }
};

}  // namespace uart::detail

#endif  // STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_
