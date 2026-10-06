// Qualification only. Never linked into the production protocol.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pqqual {
inline void wipe(void* p, size_t n) {
  auto* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
  asm volatile("" ::: "memory");
}
struct Buffers {
  uint8_t pk[1568]{}, sk[3168]{}, ct[1568]{}, ss[32]{};
  void clear() { wipe(this, sizeof(*this)); }
};
enum class Op : uint8_t { KeyGen, Encaps, Decaps };
enum class Phase : uint8_t { Empty, Queued, Running, Retiring, Result, Exhausted };
struct Token { uint64_t generation, ticket; };
using Execute = int (*)(Op, Buffers&);

// Port must serialize methods with a mutex. execute() runs OUTSIDE that mutex.
// One slot is both input and output; even a completed, unclaimed result occupies it.
// Permission represents upstream admission, not a radio authentication mechanism.
class Worker {
  Buffers slot_{};
  Phase phase_ = Phase::Empty;
  Op op_ = Op::KeyGen;
  uint64_t generation_ = 1, ticket_ = 0;
  uint64_t deadline_ = 0, next_[3]{};
  bool success_ = false;
 public:
  ~Worker() { slot_.clear(); } // Port must join/block until idle before destruction.
  Phase phase() const { return phase_; }
  uint64_t generation() const { return generation_; }
  bool clean() const {
    const auto* p = reinterpret_cast<const uint8_t*>(&slot_);
    uint8_t sum = 0; for (size_t i=0;i<sizeof(slot_);++i) sum |= p[i];
    return sum == 0; // Test-only inspection; never use for secret-dependent admission.
  }
  bool submit(Op op, bool permission, uint64_t now, const uint8_t* input,
              size_t length, const uint8_t* ciphertext, size_t ct_length, Token& t) {
    const auto i = static_cast<unsigned>(op);
    if (i > 2 || !permission || phase_ != Phase::Empty || now < next_[i] ||
        now > std::numeric_limits<uint64_t>::max()-5000) return false;
    if (op == Op::KeyGen && (input || length || ciphertext || ct_length)) return false;
    if (op == Op::Encaps && (!input || length != 1568 || ciphertext || ct_length)) return false;
    if (op == Op::Decaps && (!input || length != 3168 || !ciphertext || ct_length != 1568)) return false;
    if (ticket_ == std::numeric_limits<uint64_t>::max()) { phase_ = Phase::Exhausted; return false; }
    slot_.clear();
    if (op == Op::Encaps) std::memcpy(slot_.pk, input, 1568);
    if (op == Op::Decaps) {
      std::memcpy(slot_.sk, input, 3168); std::memcpy(slot_.ct, ciphertext, 1568);
    }
    op_=op; t={generation_,++ticket_}; deadline_=now+5000;
    next_[i]=now+1000; success_=false; phase_=Phase::Queued;
    return true;
  }
  // Safe rollback if the port's nonblocking queue send fails. Preserve the rate budget.
  void enqueue_failed(Token t) {
    if (phase_==Phase::Queued && t.generation==generation_ && t.ticket==ticket_) {
      slot_.clear(); phase_=Phase::Empty;
    }
  }
  Buffers* begin(Token t, uint64_t now, Op& op) {
    if (phase_!=Phase::Queued || t.generation!=generation_ || t.ticket!=ticket_) return nullptr;
    if (now>=deadline_) { slot_.clear(); phase_=Phase::Empty; return nullptr; }
    phase_=Phase::Running; op=op_; return &slot_;
  }
  void finish(Token t, int status, uint64_t now) {
    if (t.ticket!=ticket_) return; // Port never runs two operations.
    if (phase_==Phase::Retiring || t.generation!=generation_ || now>=deadline_) {
      slot_.clear(); success_=false;
      phase_=generation_==std::numeric_limits<uint64_t>::max()?Phase::Exhausted:Phase::Empty;
      return;
    }
    if (phase_!=Phase::Running) return;
    if (status) { slot_.clear(); success_=false; phase_=Phase::Result; return; }
    // Retain only the result required by this operation.
    if (op_!=Op::KeyGen) { wipe(slot_.sk,sizeof(slot_.sk)); wipe(slot_.pk,sizeof(slot_.pk)); }
    if (op_==Op::KeyGen) { wipe(slot_.ct,sizeof(slot_.ct)); wipe(slot_.ss,sizeof(slot_.ss)); }
    if (op_==Op::Decaps) wipe(slot_.ct,sizeof(slot_.ct));
    success_=true; phase_=Phase::Result;
  }
  bool take(Token t, uint64_t now, Buffers& out) {
    if (phase_!=Phase::Result || t.generation!=generation_ || t.ticket!=ticket_) return false;
    const bool ok=success_ && now<deadline_;
    if (ok) std::memcpy(&out,&slot_,sizeof(out));
    slot_.clear(); success_=false; phase_=Phase::Empty; return ok;
  }
  // Invalidate first. Never race a wipe against a still-running KEM routine.
  // A pending cancellation requires normal KEM return before the slot is reusable.
  void cancel() {
    if (generation_ != std::numeric_limits<uint64_t>::max()) ++generation_;
    if (phase_==Phase::Running || phase_==Phase::Retiring) { phase_=Phase::Retiring; return; }
    slot_.clear(); success_=false;
    phase_=generation_==std::numeric_limits<uint64_t>::max()?Phase::Exhausted:Phase::Empty;
  }
  void expire(uint64_t now) {
    if (phase_!=Phase::Empty && phase_!=Phase::Exhausted && now>=deadline_) cancel();
  }
};
} // namespace pqqual
