#include "hal/gpio.h"

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/detail/gpio_impl.h"
#include "stm32h7/regs/rcc.h"

namespace hal {
namespace {

// Calls f.template operator()<P>() with the port as a template argument:
// groov addresses are compile-time, the port is not.
template <typename F>
void WithPort(Port port, F&& f) noexcept {
  [&]<std::size_t... P>(std::index_sequence<P...>) {
    static_cast<void>(
        ((std::to_underlying(port) == P
              ? (f.template operator()<static_cast<Port>(P)>(), true)
              : false) ||
         ...));
  }(std::make_index_sequence<std::to_underlying(Port::kK) + 1>{});
}

// The pin number is not a compile-time field name here, so these functions
// read and write whole registers through groov, with the mask of the pin's
// field.
template <auto Map, stdx::ct_string Register>
void SetPinField(std::size_t n, std::uint32_t width, std::uint32_t value) noexcept {
  constexpr groov::path<Register> kRegister{};
  const std::uint32_t shift = static_cast<std::uint32_t>(n) * width;
  const std::uint32_t mask = ((1U << width) - 1U) << shift;
  const std::uint32_t old = groov::sync_read(Map / kRegister);
  groov::sync_write(Map(kRegister = (old & ~mask) | ((value << shift) & mask)));
}

void EnableClock(Port port) noexcept {
  // GPIOAEN..GPIOKEN are bits 0..10 of RCC_AHB4ENR.
  constexpr groov::path<"ahb4enr"> kAhb4enr{};
  constexpr auto kRcc = stm32h7::regs::rcc::kRcc;
  const std::uint32_t old = groov::sync_read(kRcc / kAhb4enr);
  groov::sync_write(kRcc(kAhb4enr = old | (1U << std::to_underlying(port))));
  static_cast<void>(groov::sync_read(kRcc / kAhb4enr));
}

}  // namespace

void ConfigureAlternate(Pin pin, Pull pull, Speed speed) noexcept {
  EnableClock(pin.port);
  WithPort(pin.port, [&]<Port P>() {
    constexpr auto kPort = detail::kPortMap<P>;
    if (pin.number < 8) {
      SetPinField<kPort, "afrl">(pin.number, 4, pin.alternate);
    } else {
      SetPinField<kPort, "afrh">(pin.number - 8U, 4, pin.alternate);
    }
    SetPinField<kPort, "ospeedr">(pin.number, 2, std::to_underlying(speed));
    SetPinField<kPort, "otyper">(pin.number, 1, 0);  // Push-pull.
    SetPinField<kPort, "pupdr">(pin.number, 2, std::to_underlying(pull));
    SetPinField<kPort, "moder">(pin.number, 2, 0b10);  // Alternate.
  });
}

void ConfigureInput(Pin pin, Pull pull) noexcept {
  EnableClock(pin.port);
  WithPort(pin.port, [&]<Port P>() {
    constexpr auto kPort = detail::kPortMap<P>;
    SetPinField<kPort, "pupdr">(pin.number, 2, std::to_underlying(pull));
    SetPinField<kPort, "moder">(pin.number, 2, 0b00);  // Input.
  });
}

bool Read(Pin pin) noexcept {
  bool level = false;
  WithPort(pin.port, [&]<Port P>() {
    const std::uint32_t idr =
        groov::sync_read(detail::kPortMap<P> / groov::path<"idr">{});
    level = ((idr >> pin.number) & 1U) != 0;
  });
  return level;
}

}  // namespace hal
