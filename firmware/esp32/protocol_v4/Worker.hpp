#pragma once
#include "Interfaces.hpp"
namespace ProtogenV4 {
// Single-thread portable reference port. RTOS adapter must serialize all
// methods and execute outside its lock. No core method calls execute().
class SlotWorker : public Worker {
public:
  enum class Phase { Empty, Queued, Running, Retiring, Ready, Exhausted };

private:
  Crypto &crypto;
  Kem &kem;
  KemBuffers buffers_{};
  Ticket token_{};
  Operation operation_ = Operation::KeyGen;
  Phase phase_ = Phase::Empty;
  uint64_t serial_ = 0;
  int status_ = -1;

public:
  SlotWorker(Crypto &c, Kem &k) : crypto(c), kem(k) {}
  ~SlotWorker() { crypto.wipe(&buffers_, sizeof(buffers_)); }
  SlotWorker(const SlotWorker &) = delete;
  SlotWorker &operator=(const SlotWorker &) = delete;
  bool submit(const Job &, Ticket &) override;
  bool poll(Result &) override;
  void cancel(uint64_t) override;
  Phase phase() const { return phase_; }
  // Future task: claim() under lock, execute() outside lock, finish() under
  // lock.
  bool claim(Ticket &);
  int execute(Ticket);
  void finish(Ticket, int);
  bool advance(); // host/simulator convenience, never called by Core.
};
} // namespace ProtogenV4
