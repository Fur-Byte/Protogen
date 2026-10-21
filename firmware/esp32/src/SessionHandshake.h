#pragma once
#include "WirelessSession.h"
namespace Wireless {
class Handshake {
public:
  enum class Phase : uint8_t {
    Idle,
    Rendezvous,
    AwaitResponse,
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
  uint8_t role = 0, helloAuth[32]{}, challengeAuth[32]{}, proofAuth[32]{},
          responseAuth[32]{}, transcript[32]{}, admissionNonce[16]{};
  Mac local{}, peer{};
  uint32_t started = 0, nextSend = 0, nextChallengeSend = 0;
  Phase phase = Phase::Idle;
  Frame hello{}, challenge{}, response{}, cached{};
  struct ChallengeReply {
    uint8_t requestHash[32]{};
    Mac source{};
  };
  ChallengeReply replies[ChallengeReplies]{};
  unsigned replyRead = 0, replyWrite = 0, replyCount = 0;
  Material material{};
  void clearCandidate();
  void retireAdmission();
  bool generate(Frame &f, Type type, uint32_t now, bool prepaid = false);
  void challengePrefix(const uint8_t requestHash[32], Frame &out) const;
  bool queueChallenge(const uint8_t requestHash[32], const Mac &source);
  void removeChallenge(const uint8_t requestHash[32], const Mac &source);
  bool authenticate(const Frame &f, const uint8_t key[32], const uint8_t *data,
                    size_t n, uint32_t now, bool finished = false);
  bool derive(const Frame &remote, uint32_t now, bool prepaid = false);
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
  // The owner retains unread RX records until cheap challenge replies drain.
  // Never discard an already delivered offer solely because this FIFO is full.
  bool canReceive() const { return replyCount < ChallengeReplies; }
  bool tick(uint32_t now, Frame &f, Mac &destination, bool &broadcast);
  void sent(const Frame &f) {
    if (phase == Phase::CommitReady && f.bytes[2] == SessionCommit) {
      phase = Phase::Complete;
      retireAdmission();
    }
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
