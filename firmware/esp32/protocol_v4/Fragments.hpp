#pragma once
#include "Derivation.hpp"
namespace ProtogenV4 {
enum class FragmentStatus { Rejected, Accepted, Complete, Conflict, Expired };
class Reassembly {
  Crypto &crypto;
  uint8_t data_[PublicKeyBytes]{};
  uint32_t received_ = 0;
  Frame descriptor_{};
  uint8_t commitment_[32]{};
  uint64_t expires_ = 0;

public:
  explicit Reassembly(Crypto &c) : crypto(c) {}
  ~Reassembly() { clear(); }
  Reassembly(const Reassembly &) = delete;
  Reassembly &operator=(const Reassembly &) = delete;
  void clear();
  bool active() const { return received_ != 0; }
  bool expired(uint64_t now) const { return active() && now >= expires_; }
  const uint8_t *bytes() const { return data_; }
  FragmentStatus accept(const Frame &, const Identity &, Mode, uint64_t, Object,
                        const uint8_t *authenticationKey, uint64_t now);
};
bool makeFragment(Crypto &, const Exchange &, Object, unsigned,
                  const uint8_t *key, Frame &);
bool structuralFragment(const Frame &, const Identity &, Mode, uint64_t,
                        Object);
} // namespace ProtogenV4
