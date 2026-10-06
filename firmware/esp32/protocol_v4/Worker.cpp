#include "Worker.hpp"
#include <cstring>
namespace ProtogenV4 {
bool SlotWorker::submit(const Job &j, Ticket &out) {
  if (phase_ != Phase::Empty || !j.generation || unsigned(j.operation) > 2)
    return false;
  if (j.operation == Operation::KeyGen &&
      (j.input || j.inputSize || j.ciphertext || j.ciphertextSize))
    return false;
  if (j.operation == Operation::Encaps &&
      (!j.input || j.inputSize != 1568 || j.ciphertext || j.ciphertextSize))
    return false;
  if (j.operation == Operation::Decaps &&
      (!j.input || j.inputSize != 3168 || !j.ciphertext ||
       j.ciphertextSize != 1568))
    return false;
  if (serial_ == UINT64_MAX) {
    phase_ = Phase::Exhausted;
    return false;
  }
  crypto.wipe(&buffers_, sizeof(buffers_));
  operation_ = j.operation;
  token_ = {j.generation, ++serial_};
  if (j.operation == Operation::Encaps)
    std::memcpy(buffers_.pk, j.input, 1568);
  if (j.operation == Operation::Decaps) {
    std::memcpy(buffers_.sk, j.input, 3168);
    std::memcpy(buffers_.ct, j.ciphertext, 1568);
  }
  out = token_;
  phase_ = Phase::Queued;
  return true;
}
bool SlotWorker::claim(Ticket &token) {
  if (phase_ != Phase::Queued)
    return false;
  token = token_;
  phase_ = Phase::Running;
  return true;
}
int SlotWorker::execute(Ticket token) {
  if (!same(token, token_) ||
      (phase_ != Phase::Running && phase_ != Phase::Retiring))
    return -1;
  return kem.execute(operation_, buffers_);
}
void SlotWorker::finish(Ticket token, int status) {
  if (!same(token, token_) ||
      (phase_ != Phase::Running && phase_ != Phase::Retiring))
    return;
  if (phase_ == Phase::Retiring) {
    crypto.wipe(&buffers_, sizeof(buffers_));
    phase_ = Phase::Empty;
    return;
  }
  status_ = status;
  if (status)
    crypto.wipe(&buffers_, sizeof(buffers_));
  else {
    if (operation_ != Operation::KeyGen) {
      crypto.wipe(buffers_.pk, 1568);
      crypto.wipe(buffers_.sk, 3168);
    }
    if (operation_ == Operation::KeyGen) {
      crypto.wipe(buffers_.ct, 1568);
      crypto.wipe(buffers_.ss, 32);
    }
    if (operation_ == Operation::Decaps)
      crypto.wipe(buffers_.ct, 1568);
  }
  phase_ = Phase::Ready;
}
bool SlotWorker::advance() {
  Ticket token;
  if (!claim(token))
    return false;
  const int status = execute(token);
  finish(token, status);
  return true;
}
bool SlotWorker::poll(Result &out) {
  if (phase_ != Phase::Ready)
    return false;
  out.ticket = token_;
  out.operation = operation_;
  out.status = status_;
  out.buffers = buffers_;
  crypto.wipe(&buffers_, sizeof(buffers_));
  phase_ = Phase::Empty;
  return true;
}
void SlotWorker::cancel(uint64_t generation) {
  if (generation != token_.generation)
    return;
  if (phase_ == Phase::Running) {
    phase_ = Phase::Retiring;
    return;
  }
  if (phase_ == Phase::Retiring)
    return;
  crypto.wipe(&buffers_, sizeof(buffers_));
  if (phase_ != Phase::Exhausted)
    phase_ = Phase::Empty;
}
} // namespace ProtogenV4
