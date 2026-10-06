#pragma once
#include "Fragments.hpp"
#include "Storage.hpp"
#include "Traffic.hpp"
namespace ProtogenV4 {
enum class State : uint8_t {
  Unpaired,
  LegacyRepairRequired,
  RecoveryRequired,
  PairingDiscovery,
  PairingPQTransfer,
  PairingCryptoPending,
  PairingDerivation,
  PairingConfirmation,
  PairingStoredGrace,
  PairedOffline,
  Rendezvous,
  AuthenticatedHello,
  PQKeyTransfer,
  PQCryptoPending,
  HybridDerivation,
  KeyConfirmation,
  Committing,
  Online,
  Rekeying,
  Recovering
};
class Core {
  Crypto &crypto;
  Clock &clock;
  Storage &storage;
  Transport &transport;
  Worker &worker;
  Hooks &hooks;
  const uint8_t role_;
  State state_ = State::Unpaired;
  PairStatus provenance_ = PairStatus::Unpaired;
  Telemetry telemetry_{};
  Budget creation_{2, 1000}, keygen_{1, 1000}, encaps_{1, 1000},
      decaps_{1, 1000}, fragments_{4, 1000}, authentication_{8, 5},
      authenticationFailures_{8, 50}, ecc_{4, 500}, derivation_{2, 1000};
  uint64_t generation_ = 1, serial_ = 0, privateHandle_ = 0, deadline_ = 0,
           pairDeadline_ = 0, lastNow_ = 0, nextSend_ = 0, identityAt_ = 0,
           lastRx_ = 0, renewAt_ = 0, keepaliveAt_ = 0, graceAt_ = 0,
           blockedUntil_ = 0, fastUntil_ = 0, controlAt_ = 0,
           transmitDeadline_ = 0;
  uint8_t psk_[32]{}, recovery_[32]{}, ecdh_[32]{}, kemSecret_[32]{},
      privateKem_[3168]{}, nonce_[16]{}, transcriptHash_[32]{},
      candidatePair_[32]{}, preAuth_[32]{}, preFragment_[32]{};
  uint32_t sas_ = 0;
  uint8_t proofs_ = 0, confirmations_ = 0, txObject_ = 0, txFragment_ = 0;
  uint8_t currentChannel_ = 6;
  Identity own_{}, peer_{};
  Exchange exchange_{};
  Reassembly reassembly_;
  DerivationWorkspace workspace_;
  Material material_{};
  Traffic traffic_;
  Result result_{};
  Ticket job_{}, transmission_{};
  Kind transmittedKind_ = Kind::Fragment;
  bool transmitting_ = false, paired_ = false, jobPending_ = false;
  Operation pendingOperation_ = Operation::KeyGen;
  uint8_t recent_[4][32]{};
  uint64_t recentAt_[4]{};
  unsigned recentWrite_ = 0;
  bool candidate() const;
  bool pairing() const;
  void transition(State);
  void clearCandidate();
  void fail(bool timeout = false);
  bool freshIdentity();
  bool beginCandidate(Mode);
  bool makeOffer();
  bool schedule(Operation);
  void result();
  bool acceptedObject(Object);
  void derive();
  bool control(Kind, Frame &);
  bool verifyControl(const Frame &, Kind);
  void receive(const Received &);
  void transmit();
  bool install();
  bool storePair();
  void recover();
  uint8_t rendezvousChannel();
  const uint8_t *fragmentKey(Object) const;

public:
  Core(uint8_t, Crypto &, Clock &, Storage &, Transport &, Worker &, Hooks &);
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;
  bool begin();
  bool openPairing();
  bool confirmPairing();
  bool sas(uint32_t &) const;
  void tick();
  void completion(Ticket, bool);
  bool send(const HelmetLink::Packet &);
  void reset();
  void requestRekey();
  void identityChanged();
  void channelLost();
  State state() const { return state_; }
  bool online() const {
    return state_ == State::Online || state_ == State::Rekeying;
  }
  const Telemetry &telemetry() const { return telemetry_; }
  PairStatus provenance() const { return provenance_; }
  uint64_t records() const { return traffic_.records(); }
  bool invariant() const;
  friend struct TestAccess;
};
} // namespace ProtogenV4
