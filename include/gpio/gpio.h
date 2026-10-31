#ifndef STM32H7_DRIVERS_INCLUDE_GPIO_GPIO_
#define STM32H7_DRIVERS_INCLUDE_GPIO_GPIO_

#include <cstdint>

namespace gpio {

// GPIO driver for GPIOA..GPIOK (RM0399, GPIO), between ST's LL and HAL:
// HAL-like configuration (Configure(), ConfigureOutput(), ...) and LL-like
// single-register operations (Set(), Toggle(), SetSpeed(), ...).
//
// Pins and ports known at compile time are class templates: Pin<kLed>,
// PortPins<Port::kB>. Their members are defined with groov in
// gpio/detail/pin_impl.h and instantiated by the application in a source
// built with -O2 (with mcu-cmake: the INSTANCES of stm32h7_drivers_add_gpio):
//
//   // board/pins.h
//   inline constexpr gpio::PinId kLed{gpio::Port::kB, 0};
//   using Led = gpio::Pin<kLed>;
//   // board/pins.cc
//   #include "gpio/detail/pin_impl.h"
//   template class gpio::Pin<board::kLed>;
//
// Then Led::Set() is a call and one store to BSRR. A pin used but not
// instantiated fails to link. Pins chosen at runtime use the free functions
// below (one port dispatch per call).
//
// Not thread-safe: configuration and the read-modify-write setters
// (SetMode(), SetSpeed(), ...) of pins of one port must not run
// concurrently. Set(), Reset(), Write() and Toggle() are single BSRR stores
// (Toggle() reads ODR first) and safe from any context.

enum class Port : std::uint8_t { kA, kB, kC, kD, kE, kF, kG, kH, kI, kJ, kK };

// MODER encodings.
enum class Mode : std::uint8_t {
  kInput = 0b00,
  kOutput = 0b01,
  kAlternate = 0b10,
  kAnalog = 0b11,  // Reset state of most pins.
};

// OTYPER encodings.
enum class OutputType : std::uint8_t { kPushPull = 0, kOpenDrain = 1 };

// PUPDR encodings.
enum class Pull : std::uint8_t { kNone = 0b00, kUp = 0b01, kDown = 0b10 };

// OSPEEDR encodings.
enum class Speed : std::uint8_t {
  kLow = 0b00,
  kMedium = 0b01,
  kHigh = 0b10,
  kVeryHigh = 0b11,
};

// A pin: port and number 0..15. Structural, so it can be a template
// argument.
struct PinId {
  Port port;
  std::uint8_t number;

  friend constexpr bool operator==(const PinId&, const PinId&) = default;
};

// HAL_GPIO_Init() equivalent. The fields not used by `mode` are ignored:
// output_type and speed for kOutput/kAlternate, alternate for kAlternate,
// initial_level for kOutput, pull for all but kAnalog.
struct Config {
  Mode mode = Mode::kInput;
  Pull pull = Pull::kNone;
  OutputType output_type = OutputType::kPushPull;
  Speed speed = Speed::kLow;
  std::uint8_t alternate = 0;  // AF0..AF15
  bool initial_level = false;  // ODR before the pin becomes an output.
};

// One pin.
template <PinId P>
class Pin {
  static_assert(P.number < 16, "GPIO pins are 0..15");

 public:
  static constexpr PinId kId = P;

  // --- Configuration (HAL level) ---------------------------------------
  // Enables the port clock and configures the pin. Speed, type, pull,
  // alternate function and the initial output level are written before
  // MODER, so the pin never drives a wrong function or level.
  static void Configure(const Config& config) noexcept;
  static void ConfigureInput(Pull pull = Pull::kNone) noexcept;
  static void ConfigureOutput(bool initial_level = false,
                              OutputType type = OutputType::kPushPull,
                              Speed speed = Speed::kLow,
                              Pull pull = Pull::kNone) noexcept;
  static void ConfigureAlternate(std::uint8_t alternate,
                                 OutputType type = OutputType::kPushPull,
                                 Speed speed = Speed::kLow,
                                 Pull pull = Pull::kNone) noexcept;
  static void ConfigureAnalog() noexcept;
  // Back to the reset configuration of most pins (analog, no pull,
  // push-pull, low speed, AF0); HAL_GPIO_DeInit() equivalent. The port
  // clock stays on.
  static void Deinit() noexcept;

  // --- Single fields (LL level) ----------------------------------------
  static void SetMode(Mode mode) noexcept;
  static Mode GetMode() noexcept;
  static void SetOutputType(OutputType type) noexcept;
  static OutputType GetOutputType() noexcept;
  static void SetSpeed(Speed speed) noexcept;
  static Speed GetSpeed() noexcept;
  static void SetPull(Pull pull) noexcept;
  static Pull GetPull() noexcept;
  static void SetAlternate(std::uint8_t alternate) noexcept;
  static std::uint8_t GetAlternate() noexcept;

  // --- Data -------------------------------------------------------------
  static bool Read() noexcept;         // Input level (IDR).
  static bool IsOutputSet() noexcept;  // Output latch (ODR).
  static void Set() noexcept;          // BSRR, one store.
  static void Reset() noexcept;        // BSRR, one store.
  static void Write(bool level) noexcept;
  static void Toggle() noexcept;  // Reads ODR, one BSRR store.

  // Locks the pin's configuration until the next reset (LCKR key
  // sequence). False if the lock did not take.
  static bool Lock() noexcept;
  static bool IsLocked() noexcept;
};

// All pins of one port, as 16-bit masks (bit n = pin n).
template <Port P>
class PortPins {
 public:
  // RCC_AHB4ENR.GPIOxEN, read back after enabling.
  static void EnableClock() noexcept;
  static void DisableClock() noexcept;
  static bool IsClockEnabled() noexcept;

  static std::uint16_t ReadInput() noexcept;   // IDR
  static std::uint16_t ReadOutput() noexcept;  // ODR
  static void WriteOutput(std::uint16_t levels) noexcept;  // ODR, all pins.
  static void Set(std::uint16_t pins) noexcept;            // BSRR, one store.
  static void Reset(std::uint16_t pins) noexcept;          // BSRR, one store.
  // Sets `set` and resets `reset` in one store (set wins on overlap).
  static void SetReset(std::uint16_t set, std::uint16_t reset) noexcept;
  static void Toggle(std::uint16_t pins) noexcept;  // Reads ODR, one store.

  static bool Lock(std::uint16_t pins) noexcept;
  static bool IsAnyLocked() noexcept;  // LCKR.LCKK
};

// --- Pins chosen at runtime ---------------------------------------------
// The same operations for a PinId value; each call dispatches on the port.
// Defined in the gpio library: no instance source needed.
void Configure(PinId pin, const Config& config) noexcept;
void Deinit(PinId pin) noexcept;
bool Read(PinId pin) noexcept;
void Write(PinId pin, bool level) noexcept;
void Toggle(PinId pin) noexcept;
void EnableClock(Port port) noexcept;

}  // namespace gpio

#endif  // STM32H7_DRIVERS_INCLUDE_GPIO_GPIO_
