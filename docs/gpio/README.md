# GPIO and EXTI drivers

Two independent drivers between ST's LL and HAL: HAL-like configuration
(`Configure()`, `ConfigureOutput()`, ...) and LL-like single-register
operations (`Set()`, `Toggle()`, `SetSpeed()`, ...), with groov on the
generated maps (`stm32h7::regs::gpio`, `exti`, `syscfg`, `rcc`). Class
diagram: `gpio_class.puml`.

## Using them

```cpp
// board/pins.h: compile-time pins (no groov in this header)
#include "exti/exti.h"
#include "gpio/gpio.h"

inline constexpr gpio::PinId kLed{gpio::Port::kB, 0};
inline constexpr gpio::PinId kButton{gpio::Port::kC, 13};
using Led = gpio::Pin<kLed>;
using Button = gpio::Pin<kButton>;
using ButtonInterrupt = exti::PinInterrupt<kButton>;

// board/pins.cc (built with -O2): the instances
#include "gpio/detail/pin_impl.h"
template class gpio::Pin<kLed>;
template class gpio::Pin<kButton>;

// board/interrupts.cc (built with -O2)
#include "exti/detail/exti_impl.h"
template class exti::PinInterrupt<kButton>;

// application code (any flags)
Led::ConfigureOutput();
Button::ConfigureInput(gpio::Pull::kDown);
ButtonInterrupt::Configure(exti::Edge::kRising, {&OnButton, nullptr}, 8);
Led::Toggle();
```

With mcu-cmake the instance sources are the `INSTANCES` of
`stm32h7_drivers_add_gpio()` and `stm32h7_drivers_add_exti()`; with
CubeMX/stm32-cmake they go through `groov_add_optimized_objects()` and the
firmware links `stm32h7_drivers::gpio` / `stm32h7_drivers::exti`. A pin that
is used but not instantiated fails to link. Pins chosen at runtime use the
free functions (`gpio::Configure(PinId, Config)`, `gpio::Write()`, ...), which
dispatch on the port and need no instance source.

Why instance sources: groov only collapses into plain loads and stores when
compiled with `-O2`, and a function-level `[[gnu::optimize("O2")]]` in a
header does not help in an `-O0` translation unit (measured: the groov
chain stays out of line). In the instance source, `Led::Set()` is one store
to BSRR; the application calls it.

## gpio (`include/gpio/`)

| | |
|---|---|
| `gpio.h` | `Port`, `Mode`, `OutputType`, `Pull`, `Speed` (register encodings), `PinId`, `Config`; `Pin<PinId>`, `PortPins<Port>`; the runtime-pin functions. |
| `detail/pin_impl.h` | Definitions with groov: named fields per pin (`moder.mode8`, `bsrr.bs8`, `afrh.afsel8`, ...), the port clock enable (`rcc.ahb4enr.gpioden`), the LCKR key sequence. |
| `source/gpio/gpio.cc` | The runtime-pin functions (port dispatch, register-level read-modify-write with the pin's mask) and the `hal::Pin` compatibility functions. |

- `Configure()` follows `HAL_GPIO_Init()`: speed and output type (output
  and alternate modes), pull (all but analog), alternate function, the
  initial output level, and MODER last, so the pin never drives a wrong
  function or level. `Deinit()` writes MODER (analog) first.
- `Set()`, `Reset()`, `Write()` are single BSRR stores (BSRR is
  `write_only<one_to_set>`: groov writes 0 to the other pins). `Toggle()`
  reads ODR, then one BSRR store; `PortPins::Toggle(mask)` likewise.
- `Lock()`: RM0399 "GPIO locking mechanism" (LCKK=1, 0, 1 with the same LCK
  bits, two reads); false if LCKK does not read 1.
- The setters (`SetMode()`, `SetSpeed()`, ...) are read-modify-writes of a
  port register: pins of one port must not be reconfigured concurrently.
- `hal::Pin` (`hal/gpio.h`, a pin with its alternate function) stays for the
  U(S)ART port descriptors; `hal::ConfigureAlternate<pin>()` forwards to
  `gpio::Pin::ConfigureAlternate()`.

## exti (`include/exti/`)

| | |
|---|---|
| `exti.h` | `Edge`, `Callback`; `Line<N>` (lines 0..31 of register set 1), `PinInterrupt<PinId>`. |
| `detail/exti_impl.h` | Definitions with groov; `Dispatch<First, Last>()`, the body of the handlers. |
| `source/exti/exti.cc` | The callback table of lines 0..15. |
| `source/exti/exti*_irq.cc` | `EXTI0_IRQHandler` .. `EXTI15_10_IRQHandler`, one source each. |

- The core's registers are chosen at compile time: `C1IMR1/C1EMR1/C1PR1` on
  the CM7, `C2*` with `CORE_CM4`.
- `PinInterrupt::Configure()` enables the SYSCFG clock, selects the port in
  `SYSCFG_EXTICRn`, sets the callback and the trigger, clears a stale
  pending flag, sets the NVIC priority, enables the NVIC interrupt and
  unmasks the line. Lines 5..9 and 10..15 share an NVIC interrupt and thus a
  priority.
- The handlers read the pending and mask registers, clear the pending lines
  of their group with one store (`CnPR1` is write 1 to clear) and call the
  callbacks, in interrupt context. Each handler is in an archive member of
  its own with an anchor function that `PinInterrupt::Configure()` refers
  to: the startup code's weak vectors would not pull a member, so without
  the anchor no handler would be linked, and with one file all seven would.
- SVD corrections (`tools/svd2groov/stm32h7.toml`): the pending registers
  are write-1-to-clear (read-write in the SVD). Register sets 2 and 3 (lines
  32..87) are not in the map yet: their SVD descriptions have more errors.
