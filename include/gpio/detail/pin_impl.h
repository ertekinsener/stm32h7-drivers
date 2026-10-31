#ifndef STM32H7_DRIVERS_INCLUDE_GPIO_DETAIL_PIN_IMPL_
#define STM32H7_DRIVERS_INCLUDE_GPIO_DETAIL_PIN_IMPL_

// Definitions of gpio::Pin and gpio::PortPins, with groov on the generated
// map (stm32h7::regs::gpio). Include only in sources built with -O2: the
// application's pin instances and the gpio library.

#include <cstdint>
#include <groov/groov.hpp>
#include <stdx/ct_string.hpp>
#include <utility>

#include "gpio/gpio.h"
#include "hal/detail/ct_name.h"
#include "stm32h7/regs/gpio.h"
#include "stm32h7/regs/rcc.h"

namespace gpio {
namespace detail {

using hal::detail::Name;

// GPIOA..GPIOK are 0x400 apart on AHB4 (RM0399, memory map).
inline constexpr std::uintptr_t kPortStride =
    stm32h7::regs::gpio::kGpiobBase - stm32h7::regs::gpio::kGpioaBase;
static_assert(stm32h7::regs::gpio::kGpiokBase ==
              stm32h7::regs::gpio::kGpioaBase + 10 * kPortStride);

// The groov map of a port.
template <Port P>
inline constexpr auto kMap =
    stm32h7::regs::gpio::kMap<stm32h7::regs::gpio::kGpioaBase +
                              std::to_underlying(P) * kPortStride>;

// "gpioaen" .. "gpioken": the port's clock enable in RCC_AHB4ENR.
template <Port P>
consteval auto ClockEnableField() {
  const char letter[1]{static_cast<char>('a' + std::to_underlying(P))};
  return stdx::ct_string{"gpio"} + stdx::ct_string<2>{letter, 1} +
         stdx::ct_string{"en"};
}

inline constexpr std::uint32_t kLckk = 1U << 16;  // LCKR lock key bit.

// The field names of pin N.
template <std::uint8_t N>
struct PinFields {
  static constexpr auto kMode = Name<"mode", N>();
  static constexpr auto kOt = Name<"ot", N>();
  static constexpr auto kOspeed = Name<"ospeed", N>();
  static constexpr auto kPupd = Name<"pupd", N>();
  static constexpr stdx::ct_string<5> kAfr = N < 8 ? "afrl" : "afrh";
  static constexpr auto kAfsel = Name<"afsel", N>();
  static constexpr auto kId = Name<"id", N>();
  static constexpr auto kOd = Name<"od", N>();
  static constexpr auto kBs = Name<"bs", N>();
  static constexpr auto kBr = Name<"br", N>();
  static constexpr auto kLck = Name<"lck", N>();
};

}  // namespace detail

// --- PortPins ---------------------------------------------------------------

template <Port P>
void PortPins<P>::EnableClock() noexcept {
  static constexpr auto kField = detail::ClockEnableField<P>();
  constexpr groov::path<"ahb4enr", kField> kEnable{};
  groov::sync_write(stm32h7::regs::rcc::kRcc(kEnable = true));
  // Read back: the clock runs before the first access to the port.
  static_cast<void>(groov::sync_read(stm32h7::regs::rcc::kRcc / kEnable));
}

template <Port P>
void PortPins<P>::DisableClock() noexcept {
  static constexpr auto kField = detail::ClockEnableField<P>();
  groov::sync_write(
      stm32h7::regs::rcc::kRcc(groov::path<"ahb4enr", kField>{} = false));
}

template <Port P>
bool PortPins<P>::IsClockEnabled() noexcept {
  static constexpr auto kField = detail::ClockEnableField<P>();
  return groov::sync_read(stm32h7::regs::rcc::kRcc /
                          groov::path<"ahb4enr", kField>{});
}

template <Port P>
std::uint16_t PortPins<P>::ReadInput() noexcept {
  return static_cast<std::uint16_t>(
      groov::sync_read(detail::kMap<P> / groov::path<"idr">{}));
}

template <Port P>
std::uint16_t PortPins<P>::ReadOutput() noexcept {
  return static_cast<std::uint16_t>(
      groov::sync_read(detail::kMap<P> / groov::path<"odr">{}));
}

template <Port P>
void PortPins<P>::WriteOutput(std::uint16_t levels) noexcept {
  groov::sync_write(detail::kMap<P>(groov::path<"odr">{} = levels));
}

template <Port P>
void PortPins<P>::Set(std::uint16_t pins) noexcept {
  groov::sync_write(
      detail::kMap<P>(groov::path<"bsrr">{} = std::uint32_t{pins}));
}

template <Port P>
void PortPins<P>::Reset(std::uint16_t pins) noexcept {
  groov::sync_write(
      detail::kMap<P>(groov::path<"bsrr">{} = std::uint32_t{pins} << 16));
}

template <Port P>
void PortPins<P>::SetReset(std::uint16_t set, std::uint16_t reset) noexcept {
  groov::sync_write(detail::kMap<P>(
      groov::path<"bsrr">{} = std::uint32_t{set} | std::uint32_t{reset} << 16));
}

template <Port P>
void PortPins<P>::Toggle(std::uint16_t pins) noexcept {
  const std::uint16_t odr = ReadOutput();
  // Set the pins that are low, reset those that are high.
  SetReset(static_cast<std::uint16_t>(~odr & pins),
           static_cast<std::uint16_t>(odr & pins));
}

// RM0399, "GPIO locking mechanism": LCKK=1, LCKK=0, LCKK=1 with the same
// LCK bits, then read LCKR twice; LCKK reads 1 once the lock is active.
template <Port P>
bool PortPins<P>::Lock(std::uint16_t pins) noexcept {
  constexpr groov::path<"lckr"> kLckr{};
  const std::uint32_t lck = pins;
  groov::sync_write(detail::kMap<P>(kLckr = detail::kLckk | lck));
  groov::sync_write(detail::kMap<P>(kLckr = lck));
  groov::sync_write(detail::kMap<P>(kLckr = detail::kLckk | lck));
  static_cast<void>(groov::sync_read(detail::kMap<P> / kLckr));
  return (groov::sync_read(detail::kMap<P> / kLckr) & detail::kLckk) != 0;
}

template <Port P>
bool PortPins<P>::IsAnyLocked() noexcept {
  return groov::sync_read(detail::kMap<P> / groov::path<"lckr", "lckk">{});
}

// --- Pin ------------------------------------------------------------------

namespace detail {

// The settings of a pin besides its mode. ConfigureAs() takes the mode as a
// template argument (only the writes of that mode are compiled), so it is
// not in here: a mode cannot be given twice.
struct PinSettings {
  Pull pull = Pull::kNone;
  OutputType output_type = OutputType::kPushPull;
  Speed speed = Speed::kLow;
  std::uint8_t alternate = 0;
  bool initial_level = false;
};

constexpr PinSettings SettingsOf(const Config& config) noexcept {
  return {.pull = config.pull,
          .output_type = config.output_type,
          .speed = config.speed,
          .alternate = config.alternate,
          .initial_level = config.initial_level};
}

// The configuration sequence of HAL_GPIO_Init() for mode M: only the writes
// that mode needs, MODER last.
template <PinId P, Mode M>
void ConfigureAs(const PinSettings& settings) noexcept {
  using F = PinFields<P.number>;
  constexpr auto kPort = kMap<P.port>;

  PortPins<P.port>::EnableClock();
  if constexpr (M == Mode::kOutput || M == Mode::kAlternate) {
    // OSPEEDR and OTYPER are separate registers: one write each.
    groov::sync_write(kPort(groov::path<"ospeedr", F::kOspeed>{} =
                                std::to_underlying(settings.speed)));
    groov::sync_write(kPort(groov::path<"otyper", F::kOt>{} =
                                settings.output_type == OutputType::kOpenDrain));
  }
  if constexpr (M != Mode::kAnalog) {
    groov::sync_write(kPort(groov::path<"pupdr", F::kPupd>{} =
                                std::to_underlying(settings.pull)));
  } else {
    groov::sync_write(kPort(groov::path<"pupdr", F::kPupd>{} = 0U));
  }
  if constexpr (M == Mode::kAlternate) {
    groov::sync_write(
        kPort(groov::path<F::kAfr, F::kAfsel>{} =
                  static_cast<std::uint8_t>(settings.alternate & 0xFU)));
  }
  if constexpr (M == Mode::kOutput) {
    Pin<P>::Write(settings.initial_level);
  }
  groov::sync_write(
      kPort(groov::path<"moder", F::kMode>{} = std::to_underlying(M)));
}

}  // namespace detail

// The mode is known here only at runtime: one switch picks the sequence.
template <PinId P>
void Pin<P>::Configure(const Config& config) noexcept {
  const detail::PinSettings settings = detail::SettingsOf(config);
  switch (config.mode) {
    case Mode::kInput:
      detail::ConfigureAs<P, Mode::kInput>(settings);
      return;
    case Mode::kOutput:
      detail::ConfigureAs<P, Mode::kOutput>(settings);
      return;
    case Mode::kAlternate:
      detail::ConfigureAs<P, Mode::kAlternate>(settings);
      return;
    case Mode::kAnalog:
      detail::ConfigureAs<P, Mode::kAnalog>(settings);
      return;
  }
}

template <PinId P>
void Pin<P>::ConfigureInput(Pull pull) noexcept {
  detail::ConfigureAs<P, Mode::kInput>({.pull = pull});
}

template <PinId P>
void Pin<P>::ConfigureOutput(bool initial_level, OutputType type, Speed speed,
                             Pull pull) noexcept {
  detail::ConfigureAs<P, Mode::kOutput>({.pull = pull,
                                         .output_type = type,
                                         .speed = speed,
                                         .initial_level = initial_level});
}

template <PinId P>
void Pin<P>::ConfigureAlternate(std::uint8_t alternate, OutputType type,
                                Speed speed, Pull pull) noexcept {
  detail::ConfigureAs<P, Mode::kAlternate>({.pull = pull,
                                            .output_type = type,
                                            .speed = speed,
                                            .alternate = alternate});
}

template <PinId P>
void Pin<P>::ConfigureAnalog() noexcept {
  detail::ConfigureAs<P, Mode::kAnalog>({});
}

template <PinId P>
void Pin<P>::Deinit() noexcept {
  using F = detail::PinFields<P.number>;
  constexpr auto kPort = detail::kMap<P.port>;
  // MODER first, so the pin stops driving before the rest changes.
  SetMode(Mode::kAnalog);
  groov::sync_write(kPort(groov::path<F::kAfr, F::kAfsel>{} = 0U));
  groov::sync_write(kPort(groov::path<"otyper", F::kOt>{} = false));
  groov::sync_write(kPort(groov::path<"ospeedr", F::kOspeed>{} = 0U));
  groov::sync_write(kPort(groov::path<"pupdr", F::kPupd>{} = 0U));
}

template <PinId P>
void Pin<P>::SetMode(Mode mode) noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"moder", F::kMode>{} =
                                             std::to_underlying(mode)));
}

template <PinId P>
Mode Pin<P>::GetMode() noexcept {
  using F = detail::PinFields<P.number>;
  const std::uint8_t value =
      groov::sync_read(detail::kMap<P.port> / groov::path<"moder", F::kMode>{});
  return static_cast<Mode>(value);
}

template <PinId P>
void Pin<P>::SetOutputType(OutputType type) noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"otyper", F::kOt>{} =
                                             type == OutputType::kOpenDrain));
}

template <PinId P>
OutputType Pin<P>::GetOutputType() noexcept {
  using F = detail::PinFields<P.number>;
  return groov::sync_read(detail::kMap<P.port> /
                          groov::path<"otyper", F::kOt>{})
             ? OutputType::kOpenDrain
             : OutputType::kPushPull;
}

template <PinId P>
void Pin<P>::SetSpeed(Speed speed) noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"ospeedr", F::kOspeed>{} =
                                             std::to_underlying(speed)));
}

template <PinId P>
Speed Pin<P>::GetSpeed() noexcept {
  using F = detail::PinFields<P.number>;
  const std::uint8_t value = groov::sync_read(
      detail::kMap<P.port> / groov::path<"ospeedr", F::kOspeed>{});
  return static_cast<Speed>(value);
}

template <PinId P>
void Pin<P>::SetPull(Pull pull) noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"pupdr", F::kPupd>{} =
                                             std::to_underlying(pull)));
}

template <PinId P>
Pull Pin<P>::GetPull() noexcept {
  using F = detail::PinFields<P.number>;
  const std::uint8_t value =
      groov::sync_read(detail::kMap<P.port> / groov::path<"pupdr", F::kPupd>{});
  return static_cast<Pull>(value);
}

template <PinId P>
void Pin<P>::SetAlternate(std::uint8_t alternate) noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(
      detail::kMap<P.port>(groov::path<F::kAfr, F::kAfsel>{} =
                               static_cast<std::uint8_t>(alternate & 0xFU)));
}

template <PinId P>
std::uint8_t Pin<P>::GetAlternate() noexcept {
  using F = detail::PinFields<P.number>;
  return groov::sync_read(detail::kMap<P.port> /
                          groov::path<F::kAfr, F::kAfsel>{});
}

template <PinId P>
bool Pin<P>::Read() noexcept {
  using F = detail::PinFields<P.number>;
  return groov::sync_read(detail::kMap<P.port> / groov::path<"idr", F::kId>{});
}

template <PinId P>
bool Pin<P>::IsOutputSet() noexcept {
  using F = detail::PinFields<P.number>;
  return groov::sync_read(detail::kMap<P.port> / groov::path<"odr", F::kOd>{});
}

// BSRR is write-1 (write_only<one_to_set>): groov writes 0 to the other
// bits, so this is a single store that leaves the other pins alone.
template <PinId P>
void Pin<P>::Set() noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"bsrr", F::kBs>{} = true));
}

template <PinId P>
void Pin<P>::Reset() noexcept {
  using F = detail::PinFields<P.number>;
  groov::sync_write(detail::kMap<P.port>(groov::path<"bsrr", F::kBr>{} = true));
}

template <PinId P>
void Pin<P>::Write(bool level) noexcept {
  if (level) {
    Set();
  } else {
    Reset();
  }
}

template <PinId P>
void Pin<P>::Toggle() noexcept {
  Write(!IsOutputSet());
}

template <PinId P>
bool Pin<P>::Lock() noexcept {
  return PortPins<P.port>::Lock(static_cast<std::uint16_t>(1U << P.number));
}

template <PinId P>
bool Pin<P>::IsLocked() noexcept {
  using F = detail::PinFields<P.number>;
  return groov::sync_read(detail::kMap<P.port> /
                          groov::path<"lckr", F::kLck>{});
}

}  // namespace gpio

#endif  // STM32H7_DRIVERS_INCLUDE_GPIO_DETAIL_PIN_IMPL_
