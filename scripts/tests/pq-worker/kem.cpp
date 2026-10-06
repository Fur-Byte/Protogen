#include "worker.hpp"
extern "C" {
#include "mlkem_native.h"
}
namespace pqqual {
int native_execute(Op op, Buffers& b) {
  switch (op) {
    case Op::KeyGen: return pqqual_keypair(b.pk,b.sk);
    case Op::Encaps: return pqqual_enc(b.ct,b.ss,b.pk);
    case Op::Decaps: return pqqual_dec(b.ss,b.ct,b.sk);
  }
  return -1;
}
}
