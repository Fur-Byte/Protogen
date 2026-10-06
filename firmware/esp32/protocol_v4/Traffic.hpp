#pragma once
#include "Interfaces.hpp"
namespace ProtogenV4 {
bool applicationValid(const HelmetLink::Packet &, uint8_t sender);
class Traffic {
  Crypto &crypto;
  uint8_t role_;
  uint8_t tx_[32]{}, rx_[32]{}, id_[8]{}, previousTx_[32]{}, previousRx_[32]{};
  uint64_t sequence_ = 0, records_ = 0, top_ = 0, window_ = 0;
  bool active_ = false;

public:
  Traffic(Crypto &c, uint8_t role) : crypto(c), role_(role) {}
  ~Traffic() {
    clear();
    crypto.wipe(previousTx_, 32);
    crypto.wipe(previousRx_, 32);
  }
  Traffic(const Traffic &) = delete;
  Traffic &operator=(const Traffic &) = delete;
  void clear();
  bool begin(const Material &);
  bool seal(const HelmetLink::Packet *, bool renew, Frame &);
  bool open(const Frame &, HelmetLink::Packet &, bool &application,
            bool &renew);
  bool needsRekey() const {
    return records_ >= RecordLimit - 1 || sequence_ >= SequenceLimit - 1024;
  }
  uint64_t records() const { return records_; }
  uint64_t sequence() const { return sequence_; }
  bool active() const { return active_; }
  friend struct TestAccess;
};
} // namespace ProtogenV4
