#ifndef EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_NX_STM32_ETH_CONFIG_
#define EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_NX_STM32_ETH_CONFIG_

// Configuration of the NetX Duo Ethernet driver (source/eth/), CONFIG of
// stm32h7_drivers_add_eth_netxduo().

#include "netxduo/lan8742.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ETH handle, initialized by the application before the IP instance is
// created (MAC address, MII/RMII mode, DMA descriptors).
extern ETH_HandleTypeDef heth;

#define eth_handle heth

#ifdef __cplusplus
}
#endif

#endif  // EXAMPLES_H755ZI_MCU_CMAKE_CONFIG_NX_STM32_ETH_CONFIG_
