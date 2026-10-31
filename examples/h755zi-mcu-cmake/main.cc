// Cortex-M7 firmware of the example: ThreadX with one thread that writes a
// line to the ST-LINK virtual COM port (USART3, 115200 8N1) every second
// through uart::ThreadxSerial.
//
// Runs on the reset clock (HSI 64 MHz) with the D-cache off, so the DMA
// buffers in D2 SRAM need no MPU setup yet. The Ethernet driver (board::eth)
// is built and linked but not used here: it needs the board's ETH and PHY
// setup (MAC address, RMII pins, DMA descriptors in D2 SRAM).

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "board/ports.h"
#include "stm32h7xx_hal.h"
#include "tx_api.h"
#include "uart/threadx_serial.h"

// ThreadX internal headers have no C++ guards of their own.
extern "C" {
#include "tx_initialize.h"
#include "tx_thread.h"

// Defined by the port (tx_timer_interrupt.S); not declared in a header.
void _tx_timer_interrupt();
}

namespace {

// Exception priorities, 4 NVIC priority bits on the STM32H7.
constexpr std::uint32_t kPendSvPriority = 15;  // lowest, required by ThreadX
constexpr std::uint32_t kSvCallPriority = 15;
constexpr std::uint32_t kSysTickPriority = 4;

constexpr UINT kHelloPriority = 10;

// Set by _tx_initialize_low_level(). HAL_Init() starts SysTick before the
// kernel exists, and _tx_timer_interrupt() must not run until then.
volatile bool tick_enabled = false;

uart::ThreadxSerial<uart::Usart3Vcp> console;

TX_THREAD hello_thread;
std::array<ULONG, 256> hello_stack;

void HelloEntry(ULONG /*input*/) {
  if (!console.Create("vcp") || !console.Configure({.baud_rate = 115'200})) {
    __builtin_trap();
  }
  constexpr std::string_view kLine = "stm32h7-drivers on mcu-cmake\r\n";
  const std::span<const std::uint8_t> line{
      reinterpret_cast<const std::uint8_t*>(kLine.data()), kLine.size()};
  while (true) {
    static_cast<void>(console.Write(line, TX_WAIT_FOREVER));
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
  }
}

}  // namespace

extern "C" void tx_application_define(void* /*first_unused_memory*/) {
  // ThreadX only stores the name pointer and never writes through it.
  tx_thread_create(&hello_thread, const_cast<CHAR*>("hello"), HelloEntry, 0,
                   hello_stack.data(), sizeof(hello_stack), kHelloPriority,
                   kHelloPriority, TX_NO_TIME_SLICE, TX_AUTO_START);
}

// Called by tx_kernel_enter() with interrupts disabled.
extern "C" void _tx_initialize_low_level() {
  // ISRs and the scheduler use the main stack; its top is the initial MSP,
  // the first word of the active vector table.
  const auto* vector_table = reinterpret_cast<const std::uint32_t*>(SCB->VTOR);
  _tx_thread_system_stack_ptr = reinterpret_cast<VOID*>(vector_table[0]);
  _tx_initialize_unused_memory = TX_NULL;

  NVIC_SetPriority(PendSV_IRQn, kPendSvPriority);
  NVIC_SetPriority(SVCall_IRQn, kSvCallPriority);

  // Same rate as the HAL tick, so HAL_GetTick() stays in milliseconds.
  SysTick_Config(SystemCoreClock / TX_TIMER_TICKS_PER_SECOND);
  NVIC_SetPriority(SysTick_IRQn, kSysTickPriority);

  tick_enabled = true;
}

extern "C" void SysTick_Handler() {
  HAL_IncTick();
  if (tick_enabled) {
    _tx_timer_interrupt();
  }
}

int main() {
  HAL_Init();
  // D2 SRAM1..3 (RAM_D2) hold the DMA buffers.
  __HAL_RCC_D2SRAM1_CLK_ENABLE();
  __HAL_RCC_D2SRAM2_CLK_ENABLE();
  __HAL_RCC_D2SRAM3_CLK_ENABLE();

  // ThreadX does not disable interrupts during kernel entry, but SysTick is
  // already running.
  __disable_irq();
  tx_kernel_enter();
}
