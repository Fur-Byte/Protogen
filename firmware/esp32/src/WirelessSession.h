#pragma once
#include "HelmetProtocol.h"
#include "ReplayWindow.h"
#include "SecurityState.h"
#include "WirelessCrypto.h"
#include "SessionPolicy.h"
namespace Wireless {
static_assert(RekeyRecords == PROTOGEN_RECORD_LIMIT &&
              MaxSequence == PROTOGEN_SEQUENCE_LIMIT &&
              SequenceRenewMargin == PROTOGEN_RENEW_MARGIN, "Session policy mismatch");
struct Material {
  uint8_t pawKey[32]{}, helmetKey[32]{}, id[8]{}, recovery[32]{}, lmk[16]{},
      pmk[16]{}, pawFinished[32]{}, helmetFinished[32]{}, pawCommit[32]{};
  Mac pawMac{}, helmetMac{};
};
bool deriveMaterial(const uint8_t ecdh[32], const uint8_t transcript[32],
                    Material &out);
class Session {
  Material keys{};
  Replay replay;
  uint64_t sequence = 0, records = 0;
  uint8_t previousPawFingerprint[32]{}, previousHelmetFingerprint[32]{};
  Diagnostics *diagnostics = nullptr;
  uint8_t role = 0;
  enum class Phase : uint8_t { Empty, Active };
  Phase phase = Phase::Empty;

public:
  explicit Session(Diagnostics *d = nullptr) : diagnostics(d) {}
  ~Session() {
    clear();
    Crypto::wipe(previousPawFingerprint, 32);
    Crypto::wipe(previousHelmetFingerprint, 32);
  }
  void clear();
  bool begin(uint8_t ownRole, const Material &material);
  uint64_t recordCount() const { return records; }
  bool needsRekey() const {
    return protogenNeedsRekey(records, sequence);
  }
  bool seal(const HelmetLink::Packet &p, Frame &out);
  bool open(const uint8_t *bytes, size_t n, HelmetLink::Packet &p);
  bool ready() const { return phase == Phase::Active; }
};
// Directional allowlist, checked BEFORE forwarding anything to the Teensy.
bool applicationValid(const HelmetLink::Packet &p, uint8_t sender);
} // namespace Wireless
