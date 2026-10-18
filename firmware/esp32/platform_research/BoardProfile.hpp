#pragma once
#include <cstdint>
// Research contracts only. There is no default board mapping or production selector.
namespace ProtogenResearch {
enum class Target : uint8_t { Unspecified, Esp32S31 };
enum class Role : uint8_t { Unspecified, Helmet, Paw };
enum class ProfileError : uint8_t {
  None, Unreviewed, WrongTarget, UnsupportedRole, MemoryUnspecified,
  UartUnavailable, PinConflict, PinUnavailable, ConsoleConflict
};
struct Capabilities {
  uint64_t inputs=0, outputs=0;
  unsigned highPowerUarts=0;
};
struct BoardProfile {
  const char *name=nullptr;
  Target target=Target::Unspecified;
  Role role=Role::Unspecified;
  bool reviewed=false, psramReviewed=false;
  uint32_t flashBytes=0, psramBytes=0, baud=115200;
  int uartPort=-1, rx=-1, tx=-1, consolePort=-1;
  // Supplied by the reviewed module/schematic, never inferred from the chip.
  // Exclude flash/PSRAM, straps, USB/JTAG and all other fitted peripheral uses.
  uint64_t exposedInputs=0, exposedOutputs=0, reservedPins=0, consolePins=0;
};
constexpr uint64_t pinBit(int pin) {
  return pin>=0 && pin<64 ? uint64_t(1)<<unsigned(pin) : 0;
}
inline ProfileError validate(const BoardProfile &p,const Capabilities &c) {
  if (!p.reviewed || !p.name || !p.name[0]) return ProfileError::Unreviewed;
  if (p.target!=Target::Esp32S31) return ProfileError::WrongTarget;
  // This milestone is specifically the helmet-to-Teensy UART. It does not
  // assign a UART to the Paw or claim to validate its controls/ADC pinout.
  if (p.role!=Role::Helmet) return ProfileError::UnsupportedRole;
  if (!p.flashBytes || (p.psramBytes && !p.psramReviewed)) return ProfileError::MemoryUnspecified;
  if (p.uartPort<0 || unsigned(p.uartPort)>=c.highPowerUarts || p.baud!=115200)
    return ProfileError::UartUnavailable;
  if (p.rx==p.tx) return ProfileError::PinConflict;
  const auto rx=pinBit(p.rx),tx=pinBit(p.tx);
  if (!rx || !tx || !(c.inputs & p.exposedInputs & rx) ||
      !(c.outputs & p.exposedOutputs & tx) || ((rx|tx)&p.reservedPins))
    return ProfileError::PinUnavailable;
  if (p.uartPort==p.consolePort || ((rx|tx)&p.consolePins))
    return ProfileError::ConsoleConflict;
  return ProfileError::None;
}
} // namespace ProtogenResearch
