#ifndef STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_
#define STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_

#include <cstdint>
#include <utility>

#include "dma/types.h"
#include "stm32h7xx_hal.h"
#include "uart/types.h"

namespace uart::detail {

// Kernel clock domain of an instance: USART1/6 run from D2 PCLK2, all others
// from D2 PCLK1. The driver selects PCLK as kernel clock (D2CCIP2R = 0).
enum class ClockDomain : std::uint8_t { kApb1, kApb2 };

struct InstanceInfo {
  std::uintptr_t base;
  IRQn_Type irq;
  dma::Request rx_request;
  dma::Request tx_request;
  ClockDomain domain;
  std::uint32_t enable_bit;  // In RCC->APB1LENR or RCC->APB2ENR.
};

consteval InstanceInfo Describe(Instance instance) {
  switch (instance) {
    case Instance::kUsart1:
      return {USART1_BASE, USART1_IRQn, dma::Request::kUsart1Rx,
              dma::Request::kUsart1Tx, ClockDomain::kApb2,
              RCC_APB2ENR_USART1EN};
    case Instance::kUsart2:
      return {USART2_BASE, USART2_IRQn, dma::Request::kUsart2Rx,
              dma::Request::kUsart2Tx, ClockDomain::kApb1,
              RCC_APB1LENR_USART2EN};
    case Instance::kUsart3:
      return {USART3_BASE, USART3_IRQn, dma::Request::kUsart3Rx,
              dma::Request::kUsart3Tx, ClockDomain::kApb1,
              RCC_APB1LENR_USART3EN};
    case Instance::kUart4:
      return {UART4_BASE, UART4_IRQn, dma::Request::kUart4Rx,
              dma::Request::kUart4Tx, ClockDomain::kApb1,
              RCC_APB1LENR_UART4EN};
    case Instance::kUart5:
      return {UART5_BASE, UART5_IRQn, dma::Request::kUart5Rx,
              dma::Request::kUart5Tx, ClockDomain::kApb1,
              RCC_APB1LENR_UART5EN};
    case Instance::kUsart6:
      return {USART6_BASE, USART6_IRQn, dma::Request::kUsart6Rx,
              dma::Request::kUsart6Tx, ClockDomain::kApb2,
              RCC_APB2ENR_USART6EN};
    case Instance::kUart7:
      return {UART7_BASE, UART7_IRQn, dma::Request::kUart7Rx,
              dma::Request::kUart7Tx, ClockDomain::kApb1,
              RCC_APB1LENR_UART7EN};
    case Instance::kUart8:
      return {UART8_BASE, UART8_IRQn, dma::Request::kUart8Rx,
              dma::Request::kUart8Tx, ClockDomain::kApb1,
              RCC_APB1LENR_UART8EN};
  }
  std::unreachable();
}

template <Instance I>
struct InstanceTraits {
  static constexpr InstanceInfo kInfo = Describe(I);
  static constexpr std::uintptr_t kBase = kInfo.base;
  static constexpr IRQn_Type kIrq = kInfo.irq;
  static constexpr dma::Request kRxRequest = kInfo.rx_request;
  static constexpr dma::Request kTxRequest = kInfo.tx_request;

  // Enables the bus clock and selects PCLK as kernel clock.
  static void EnableClock() noexcept {
    if constexpr (kInfo.domain == ClockDomain::kApb2) {
      CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_USART16SEL);
      SET_BIT(RCC->APB2ENR, kInfo.enable_bit);
      static_cast<void>(READ_BIT(RCC->APB2ENR, kInfo.enable_bit));
    } else {
      CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_USART28SEL);
      SET_BIT(RCC->APB1LENR, kInfo.enable_bit);
      static_cast<void>(READ_BIT(RCC->APB1LENR, kInfo.enable_bit));
    }
  }

  static std::uint32_t KernelClockHz() noexcept {
    if constexpr (kInfo.domain == ClockDomain::kApb2) {
      return HAL_RCC_GetPCLK2Freq();
    } else {
      return HAL_RCC_GetPCLK1Freq();
    }
  }
};

}  // namespace uart::detail

#endif  // STM32H7_DRIVERS_INCLUDE_UART_DETAIL_INSTANCE_TRAITS_
