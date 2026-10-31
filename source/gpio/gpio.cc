#include "gpio/gpio.h"

#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "gpio/detail/pin_impl.h"
#include "hal/gpio.h"

namespace gpio {
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

// The pin number is not a compile-time field name here: these read and
// write whole registers through groov, with the mask of the pin's field.
template <Port P, stdx::ct_string Register>
void SetPinField(std::size_t n, std::uint32_t width, std::uint32_t value) noexcept {
  constexpr groov::path<Register> kRegister{};
  constexpr auto kPort = detail::kMap<P>;
  const auto shift = static_cast<std::uint32_t>(n) * width;
  const std::uint32_t mask = ((1U << width) - 1U) << shift;
  const std::uint32_t old = groov::sync_read(kPort / kRegister);
  groov::sync_write(kPort(kRegister = (old & ~mask) | ((value << shift) & mask)));
}

template <Port P>
void SetMode(std::size_t n, Mode mode) noexcept {
  SetPinField<P, "moder">(n, 2, std::to_underlying(mode));
}

template <Port P>
void SetAlternate(std::size_t n, std::uint8_t alternate) noexcept {
  if (n < 8) {
    SetPinField<P, "afrl">(n, 4, alternate);
  } else {
    SetPinField<P, "afrh">(n - 8, 4, alternate);
  }
}

}  // namespace

void Configure(PinId pin, const Config& config) noexcept {
  WithPort(pin.port, [&]<Port P>() {
    const std::size_t n = pin.number;
    PortPins<P>::EnableClock();
    if (config.mode == Mode::kOutput || config.mode == Mode::kAlternate) {
      SetPinField<P, "ospeedr">(n, 2, std::to_underlying(config.speed));
      SetPinField<P, "otyper">(n, 1, std::to_underlying(config.output_type));
    }
    const Pull pull = config.mode == Mode::kAnalog ? Pull::kNone : config.pull;
    SetPinField<P, "pupdr">(n, 2, std::to_underlying(pull));
    if (config.mode == Mode::kAlternate) {
      SetAlternate<P>(n, config.alternate);
    }
    if (config.mode == Mode::kOutput) {
      const auto bit = static_cast<std::uint16_t>(1U << n);
      PortPins<P>::SetReset(config.initial_level ? bit : 0,
                            config.initial_level ? 0 : bit);
    }
    SetMode<P>(n, config.mode);
  });
}

void Deinit(PinId pin) noexcept {
  WithPort(pin.port, [&]<Port P>() {
    const std::size_t n = pin.number;
    SetMode<P>(n, Mode::kAnalog);
    SetAlternate<P>(n, 0);
    SetPinField<P, "otyper">(n, 1, 0);
    SetPinField<P, "ospeedr">(n, 2, 0);
    SetPinField<P, "pupdr">(n, 2, 0);
  });
}

bool Read(PinId pin) noexcept {
  bool level = false;
  WithPort(pin.port, [&]<Port P>() {
    level = ((PortPins<P>::ReadInput() >> pin.number) & 1U) != 0;
  });
  return level;
}

void Write(PinId pin, bool level) noexcept {
  WithPort(pin.port, [&]<Port P>() {
    const auto bit = static_cast<std::uint16_t>(1U << pin.number);
    PortPins<P>::SetReset(level ? bit : 0, level ? 0 : bit);
  });
}

void Toggle(PinId pin) noexcept {
  WithPort(pin.port, [&]<Port P>() {
    PortPins<P>::Toggle(static_cast<std::uint16_t>(1U << pin.number));
  });
}

void EnableClock(Port port) noexcept {
  WithPort(port, [&]<Port P>() { PortPins<P>::EnableClock(); });
}

}  // namespace gpio

// --- hal::gpio compatibility (hal/gpio.h) -----------------------------------

namespace hal {

void ConfigureAlternate(Pin pin, Pull pull, Speed speed) noexcept {
  gpio::Configure({pin.port, pin.number}, {.mode = gpio::Mode::kAlternate,
                                           .pull = pull,
                                           .speed = speed,
                                           .alternate = pin.alternate});
}

void ConfigureInput(Pin pin, Pull pull) noexcept {
  gpio::Configure({pin.port, pin.number},
                  {.mode = gpio::Mode::kInput, .pull = pull});
}

bool Read(Pin pin) noexcept { return gpio::Read({pin.port, pin.number}); }

}  // namespace hal
