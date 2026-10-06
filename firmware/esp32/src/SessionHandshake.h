#pragma once
#include "WirelessSession.h"
namespace Wireless {
class Handshake {
public:
  enum class Phase : uint8_t {
    Idle,
    Rendezvous,
    AuthenticatedEcdh,
    AwaitPawFinished,
    AwaitHelmetFinished,
    AwaitCommit,
    CommitReady,
    Complete,
    Failed
  };

private:
  CryptoBudget ownedBudget;
  CryptoBudget *budget;
  Crypto::Agreement agreement;
  uint8_t role = 0, helloAuth[32]{}, responseAuth[32]{}, transcript[32]{};
  Mac local{}, peer{};
  uint32_t started = 0, nextSend = 0;
  Phase phase = Phase::Idle;
  Frame hello{}, response{}, cached{};
  struct RecentHello {
    uint8_t hash[32]{};
    uint32_t at = 0;
    bool valid = false;
  };
  RecentHello recent[4]{};
  unsigned recentAt = 0;
  Material material{};
  bool generate(Frame &f, Type type, uint32_t now);
  bool authenticate(const Frame &f, const uint8_t key[32], const uint8_t *data,
                    size_t n, uint32_t now, bool finished = false);
  bool derive(const Frame &remote, uint32_t now);
  bool finished(Type type);
  void fail(uint32_t now, bool keyConfirmation = false);

public:
  explicit Handshake(CryptoBudget *b = nullptr)
      : budget(b ? b : &ownedBudget) {}
  ~Handshake() { clear(); }
  void clear();
  bool start(const uint8_t pairingSecret[32], uint8_t ownRole, const Mac &own,
             uint32_t now);
  bool receive(const Frame &f, const Mac &source, uint32_t now);
  bool tick(uint32_t now, Frame &f, Mac &destination, bool &broadcast);
  void sent(const Frame &f) {
    if (phase == Phase::CommitReady && f.bytes[2] == SessionCommit)
      phase = Phase::Complete;
  }
  bool ready() const { return phase == Phase::Complete; }
  bool failed() const { return phase == Phase::Failed; }
  const Material &keys() const { return material; }
  bool pending() const {
    return phase != Phase::Idle && phase != Phase::Rendezvous &&
           phase != Phase::Failed;
  }
  Phase state() const { return phase; }
};
} // namespace Wireless
