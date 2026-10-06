#include "Simulator.hpp"
#include <fstream>
#include <sstream>
#include <string>
using namespace ProtogenV4;
using namespace V4Test;
namespace {
std::string hex(const uint8_t *p, size_t n) {
  std::string s;
  constexpr char digits[] = "0123456789abcdef";
  while (n--) {
    s += digits[*p >> 4];
    s += digits[*p++ & 15];
  }
  return s;
}
std::string expected(const std::string &json, const char *name) {
  const std::string prefix = std::string("\"") + name + "\": \"";
  const auto at = json.find(prefix);
  assert(at != std::string::npos);
  const auto start = at + prefix.size();
  return json.substr(start, json.find('"', start) - start);
}
} // namespace
void vectors() {
  std::ifstream input(V4_VECTOR_FILE);
  assert(input.good());
  std::stringstream contents;
  contents << input.rdbuf();
  auto json = contents.str();
  TestCrypto c(10);
  Exchange x;
  x.mode = Mode::Session;
  x.wire = 0x123456789abcdef0ULL;
  x.complete = 0x3f;
  x.paw.bytes[0] = x.helmet.bytes[0] = 2;
  x.paw.bytes[5] = 3;
  x.helmet.bytes[5] = 4;
  for (unsigned i = 0; i < 6; ++i)
    for (size_t j = 0; j < objectSize(Object(i)); ++j)
      x.objects[i][j] = uint8_t(j + 17 * i);
  DerivationWorkspace w;
  uint8_t e[32], k[32], root[32], hash[32], changed[32];
  for (unsigned i = 0; i < 32; ++i) {
    e[i] = uint8_t(i + 1);
    k[i] = uint8_t(i + 33);
  }
  size_t length = canonical(c, x, w.transcript, sizeof(w.transcript));
  assert(length == 3825);
  assert(c.hash(w.transcript, length, hash) &&
         hex(hash, 32) == expected(json, "transcript_sha256"));
  assert(hybridRoot(c, e, k, w.transcript, length, root, w) &&
         hex(root, 32) == expected(json, "hybrid_root"));
  Material m;
  assert(deriveMaterial(c, root, hash, m));
  const auto check = [&](const uint8_t *p, size_t n, const char *name) {
    assert(hex(p, n) == expected(json, name));
  };
  check(m.pawKey, 32, "paw-to-helmet/aes-gcm");
  check(m.helmetKey, 32, "helmet-to-paw/aes-gcm");
  check(m.id, 8, "id");
  check(m.recovery, 32, "recovery");
  check(m.lmk, 16, "esp-now/lmk");
  check(m.pmk, 16, "esp-now/pmk");
  check(m.pawFinished, 32, "finished/paw");
  check(m.helmetFinished, 32, "finished/helmet");
  check(m.commit, 32, "finished/paw-commit");
  check(m.commitAck, 32, "finished/helmet-commit-ack");
  check(m.handshakeAuth, 32, "handshake/auth");
  check(m.fragmentAuth, 32, "handshake/fragments");
  // Identities include documented unicast/local and direction bits.
  assert(hex(m.pawIdentity.bytes, 6) == "b60f7c239ba7" &&
         hex(m.helmetIdentity.bytes, 6) == "26872c3d6252");
  Simulation s;
  TestAccess::vectorContext(s.paw.core, x, m, hash);
  TestAccess::vectorContext(s.helmet.core, x, m, hash);
  for (auto kind : {Kind::PawFinished, Kind::HelmetFinished, Kind::Commit,
                    Kind::CommitAck}) {
    Frame f;
    auto &core = kind == Kind::PawFinished || kind == Kind::Commit
                     ? s.paw.core
                     : s.helmet.core;
    assert(TestAccess::confirmation(core, kind, f));
    check(f.bytes + 96, 32, std::to_string(unsigned(kind)).c_str());
  }
  e[0] ^= 1;
  assert(hybridRoot(c, e, k, w.transcript, length, changed, w) &&
         !equal(root, changed, 32));
  e[0] ^= 1;
  k[0] ^= 1;
  assert(hybridRoot(c, e, k, w.transcript, length, changed, w) &&
         !equal(root, changed, 32));
  k[0] ^= 1;
  // Protocol context, public key/ciphertext, role, metadata and nonce
  // mutations.
  for (auto offset : {size_t(24), size_t(26), size_t(42), size_t(200),
                      size_t(2000), length - 1}) {
    w.transcript[offset] ^= 1;
    assert(hybridRoot(c, e, k, w.transcript, length, changed, w) &&
           !equal(root, changed, 32));
    w.transcript[offset] ^= 1;
  }
  c.wipe(e, 32);
  assert(!hybridRoot(c, e, k, w.transcript, length, changed, w) &&
         zero(changed, 32));
  assert(!hybridRoot(c, k, k, w.transcript, 4097, changed, w));
  Traffic paw(c, Paw), helmet(c, Helmet);
  assert(paw.begin(m) && helmet.begin(m));
  Frame f;
  assert(paw.seal(nullptr, false, f));
  HelmetLink::Packet p;
  bool app, renew;
  assert(helmet.open(f, p, app, renew) && !app && !renew);
  assert(!helmet.open(f, p, app, renew));
  assert(!paw.begin(m) && !paw.active());
  // SHA3-256 published empty-string value (FIPS 202 example), independent
  // oracle.
  assert(
      c.sha3(nullptr, 0, changed) &&
      hex(changed, 32) ==
          "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
  // P-256 d=1 times its generator is the standardized generator x-coordinate.
  const uint8_t point[65] = {
      4,    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc,
      0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d,
      0xeb, 0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
      0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb,
      0x4a, 0x7c, 0x0f, 0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31,
      0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5};
  uint8_t scalar[32]{};
  scalar[31] = 1;
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t id = 0;
  psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&a, PSA_ALG_ECDH);
  assert(psa_import_key(&a, scalar, 32, &id) == PSA_SUCCESS);
  assert(c.ecdh(id, point, changed) && equal(changed, point + 1, 32));
  psa_destroy_key(id);
  psa_reset_key_attributes(&a);
  c.wipe(scalar, 32);
  // RFC 4231 case 1, key is 20 bytes of 0b padded with zero to HMAC's block.
  uint8_t hkey[32]{};
  for (unsigned i = 0; i < 20; ++i)
    hkey[i] = 0x0b;
  assert(
      c.hmac(hkey, "Hi There", 8, changed) &&
      hex(changed, 32) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  c.wipe(&m, sizeof(m));
  c.wipe(&w, sizeof(w));
  c.wipe(k, 32);
  c.wipe(root, 32);
  c.wipe(changed, 32);
  std::puts("v4 deterministic transcript/combiner/keys/Finished/Commit and "
            "primitive vectors passed");
}
