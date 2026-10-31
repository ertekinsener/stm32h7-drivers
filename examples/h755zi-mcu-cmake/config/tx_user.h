#ifndef EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_TX_USER_
#define EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_TX_USER_

// ThreadX configuration, CONFIG of threadx_add_kernel(). SysTick drives the
// HAL tick and the ThreadX tick at the same rate (main.cc).

#define TX_TIMER_TICKS_PER_SECOND 1000
#define TX_TIMER_PROCESS_IN_ISR
#define TX_ENABLE_STACK_CHECKING

#endif  // EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_TX_USER_
