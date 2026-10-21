#pragma once
#include "HelmetProtocol.h"
#include "SecureZero.h"
namespace UartSecurity {
constexpr uint8_t Version = 3, Esp = 1, Teensy = 2;
constexpr size_t Header = 32, Tag = 32,
                 MaxRaw = Header + HelmetLink::MaxRaw + Tag + 2,
                 MaxEncoded = MaxRaw + 2;
constexpr uint32_t RetryMs = 250, CandidateMs = 2000, RequestLimitMs = 1000;
constexpr uint64_t MaxSequence = 0x7fffffffffffffffULL;
enum Type : uint8_t { Request = 1, Challenge = 2, Confirm = 3, Data = 4 };
enum class State : uint8_t { Idle, AwaitChallenge, AwaitStatus, Online };
using MacFunction = bool (*)(void *, const uint8_t *, size_t, uint8_t[32]);
using RandomFunction = bool (*)(void *, uint8_t *, size_t);
inline uint64_t get64(const uint8_t *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; ++i)
    v |= uint64_t(p[i]) << (i * 8);
  return v;
}
inline void put64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = uint8_t(v >> (i * 8));
}
inline bool equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t v = 0;
  while (n--)
    v |= *a++ ^ *b++;
  return v == 0;
}
// The adapter owns the long-term HMAC key. A volatile receiver seed binds
// stateless challenges to this boot; it is wiped on clear and never exported.
class Endpoint {
  uint8_t role;
  State phase = State::Idle;
  void *context;
  MacFunction mac;
  RandomFunction random;
  uint8_t id[16]{}, clientNonce[16]{};
  uint8_t challengeSeed[16]{};
  uint32_t generation = 0, seedAttemptAt = 0, clockAt = 0, clockEra = 0;
  bool seedReady = false, seedAttempted = false;
  uint32_t epoch = 0, nextControl = 0, lastLive = 0;
  uint64_t transmit = 0, received = 0;
  uint8_t encoded[MaxEncoded]{};
  size_t used = 0;
  uint32_t frameAt = 0;
  bool discarding = false, handledControl = false;
  uint8_t control[MaxRaw]{};
  size_t controlLength = 0;
  bool sign(uint8_t *raw, size_t n) {
    // Explicit domain and direction are included in every authentication input.
    uint8_t input[24 + MaxRaw]{};
    static constexpr char label[] = "protogen/v3/uart/auth";
    memcpy(input, label, sizeof(label) - 1);
    memcpy(input + 24, raw, n);
    const bool okay = mac(context, input, 24 + n, raw + n);
    protogenSecureZero(input, sizeof(input));
    return okay;
  }
  size_t make(uint8_t *out, Type type, const uint8_t sid[16], uint32_t ep,
              uint64_t seq, const uint8_t *payload, size_t n) {
    if (n > HelmetLink::MaxRaw)
      return 0;
    memset(out, 0, MaxRaw);
    out[0] = 0x75;
    out[1] = Version;
    out[2] = role;
    out[3] = type;
    memcpy(out + 4, sid, 16);
    HelmetLink::put32(out + 20, ep);
    put64(out + 24, seq);
    memcpy(out + Header, payload, n);
    const size_t authenticated = Header + n;
    if (!sign(out, authenticated))
      return 0;
    HelmetLink::put16(out + authenticated + Tag,
                      HelmetLink::crc16(out, authenticated + Tag));
    return authenticated + Tag + 2;
  }
  void advanceClock(uint32_t now) {
    // poll/feed are serviced continuously. Retaining the wrap era prevents a
    // never-committed recording becoming fresh again after a millis wrap.
    if (now < clockAt && clockEra != UINT32_MAX) ++clockEra;
    clockAt = now;
  }
  bool readySeed(uint32_t now) {
    if (seedReady) return true;
    if (seedAttempted && now - seedAttemptAt < RequestLimitMs) return false;
    seedAttempted = true; seedAttemptAt = now;
    if (!random(context, challengeSeed, sizeof(challengeSeed))) {
      protogenSecureZero(challengeSeed, sizeof(challengeSeed)); return false;
    }
    uint8_t value = 0;
    for (auto b : challengeSeed) value |= b;
    seedReady = value != 0;
    return seedReady;
  }
  bool challengeId(const uint8_t nonce[16], uint32_t issued, uint32_t era,
                   uint8_t out[16]) {
    static constexpr char label[] = "protogen/v3/uart/challenge-id";
    uint8_t input[sizeof(label) - 1 + 16 + 16 + 16]{};
    size_t at = sizeof(label) - 1;
    memcpy(input, label, at);
    memcpy(input + at, challengeSeed, 16); at += 16;
    memcpy(input + at, nonce, 16); at += 16;
    HelmetLink::put32(input + at, epoch); at += 4;
    HelmetLink::put32(input + at, generation); at += 4;
    HelmetLink::put32(input + at, issued); at += 4;
    HelmetLink::put32(input + at, era);
    uint8_t tag[32]{};
    const bool okay = mac(context, input, sizeof(input), tag);
    if (okay) {
      // Public SID: exact issue time and 96 PRF bits. This is NOT the frame
      // authentication tag; every frame still carries its full 256-bit HMAC.
      HelmetLink::put32(out, issued); memcpy(out + 4, tag, 12);
    }
    protogenSecureZero(input, sizeof(input));
    protogenSecureZero(tag, sizeof(tag));
    return okay;
  }
  bool decode(const uint8_t *b, size_t n, uint32_t now,
              HelmetLink::Packet &packet) {
    if (n < Header + Tag + 2 || n > MaxRaw || b[0] != 0x75 || b[1] != Version ||
        b[2] != (role == Esp ? Teensy : Esp) || b[3] < Request || b[3] > Data ||
        HelmetLink::u16(b + n - 2) != HelmetLink::crc16(b, n - 2))
      return false;
    const auto type = Type(b[3]);
    const size_t payload = n - Header - Tag - 2;
    const uint64_t seq = get64(b + 24);
    const uint32_t ep = HelmetLink::u32(b + 20);
    if (type == Data && (phase != State::Online &&
                         !(role == Esp && phase == State::AwaitStatus)))
      return false;
    if (type == Data && (!seq || seq > MaxSequence || seq <= received ||
                         ep != epoch || !equal(b + 4, id, 16)))
      return false;
    if (type != Data && (seq || payload != 16))
      return false;
    if ((type == Request || type == Confirm) && role != Teensy)
      return false;
    if (type == Challenge && (role != Esp || phase != State::AwaitChallenge ||
                              !equal(b + Header, clientNonce, 16)))
      return false;
    if (type == Request || type == Confirm) {
      if (!epoch) return false;
      if (type == Request) {
        for (unsigned i = 4; i < Header; ++i)
          if (b[i])
            return false;
      } else {
        if (ep != epoch || !seedReady || generation == UINT32_MAX ||
            now - HelmetLink::u32(b + 4) >= CandidateMs) return false;
      }
    }
    // Structural checks precede the bounded MAC operation. Tags never escape.
    uint8_t copy[MaxRaw]{};
    memcpy(copy, b, n - Tag - 2);
    const bool authenticated = sign(copy, n - Tag - 2) &&
                               equal(copy + n - Tag - 2, b + n - Tag - 2, Tag);
    protogenSecureZero(copy, sizeof(copy));
    if (!authenticated)
      return false;
    if (type == Request) {
      // No Request owns a candidate slot or can invalidate an earlier Confirm.
      // Wire service bounds short HMAC work; RNG is needed only once per boot.
      uint8_t sid[16]{};
      if (!readySeed(now) || generation == UINT32_MAX ||
          !challengeId(b + Header, now, clockEra, sid)) return false;
      controlLength = make(control, Challenge, sid, epoch, 0, b + Header, 16);
      protogenSecureZero(sid, sizeof(sid));
      nextControl = now;
      handledControl = true;
      return false;
    }
    if (type == Challenge) {
      if (!ep)
        return false;
      memcpy(id, b + 4, 16);
      epoch = ep;
      lastLive = now;
      transmit = received = 0;
      phase = State::AwaitStatus;
      controlLength = make(control, Confirm, id, epoch, 0, clientNonce, 16);
      nextControl = now;
      handledControl = true;
      return false;
    }
    if (type == Confirm) {
      const uint32_t issued = HelmetLink::u32(b + 4);
      if (issued > now && !clockEra) return false;
      const uint32_t issuedEra = issued > now ? clockEra - 1 : clockEra;
      uint8_t expected[16]{};
      const bool matches = challengeId(b + Header, issued, issuedEra, expected) &&
                           equal(expected, b + 4, sizeof(expected));
      protogenSecureZero(expected, sizeof(expected));
      if (!matches) return false;
      memcpy(id, b + 4, 16);
      memcpy(clientNonce, b + Header, 16);
      ++generation; // Retires every prior challenge without resetting Data twice.
      transmit = received = 0;
      phase = State::Online;
      controlLength = 0;
      return false;
    }
    HelmetLink::Packet decoded;
    if (!HelmetLink::unpack(b + Header, payload, decoded) ||
        decoded.epoch != epoch)
      return false;
    if (role == Teensy && decoded.kind != HelmetLink::Hello &&
        decoded.kind != HelmetLink::Heartbeat &&
        decoded.kind != HelmetLink::Command)
      return false;
    if (role == Esp && decoded.kind != HelmetLink::Status &&
        decoded.kind != HelmetLink::Ack)
      return false;
    received = seq;
    lastLive = now;
    phase = State::Online;
    controlLength = 0;
    packet = decoded;
    return true;
  }

public:
  uint32_t errors = 0;
  Endpoint(uint8_t r, void *c, MacFunction m, RandomFunction rng)
      : role(r), context(c), mac(m), random(rng) {}
  ~Endpoint() { clear(); }
  Endpoint(const Endpoint &) = delete;
  Endpoint &operator=(const Endpoint &) = delete;
  void clear() {
    phase = State::Idle;
    seedReady = seedAttempted = false;
    generation = seedAttemptAt = clockAt = clockEra = 0;
    protogenSecureZero(challengeSeed, sizeof(challengeSeed));
    controlLength = used = 0;
    transmit = received = 0;
    protogenSecureZero(id, 16);
    protogenSecureZero(clientNonce, 16);
    protogenSecureZero(encoded, sizeof(encoded));
    protogenSecureZero(control, sizeof(control));
  }
  bool begin(uint32_t bootEpoch, uint32_t now) {
    clear();
    epoch = bootEpoch;
    clockAt = now;
    lastLive = now;
    if (role == Teensy)
      return epoch != 0;
    if (!random(context, clientNonce, 16))
      return false;
    uint8_t empty[16]{};
    controlLength = make(control, Request, empty, 0, 0, clientNonce, 16);
    phase = State::AwaitChallenge;
    nextControl = now;
    return controlLength != 0;
  }
  bool online() const { return phase == State::Online; }
  size_t poll(uint32_t now, uint8_t *out, size_t capacity) {
    advanceClock(now);
    if (role == Esp &&
        (phase == State::Online || phase == State::AwaitStatus) &&
        now - lastLive >= CandidateMs)
      begin(0, now);
    if (role == Teensy && controlLength && control[3] == Challenge &&
        now - HelmetLink::u32(control + 4) >= CandidateMs) controlLength = 0;
    if (!controlLength || int32_t(now - nextControl) < 0)
      return 0;
    nextControl = now + RetryMs;
    const size_t n =
        HelmetLink::cobsEncode(control, controlLength, out, capacity);
    if (role == Teensy)
      controlLength = 0;
    return n;
  }
  size_t seal(const HelmetLink::Packet &p, uint8_t *out, size_t capacity) {
    if (!online() || p.epoch != epoch || transmit >= MaxSequence)
      return 0;
    uint8_t payload[HelmetLink::MaxRaw]{}, raw[MaxRaw]{};
    const size_t n = HelmetLink::pack(p, payload, sizeof(payload));
    size_t result = 0;
    if (n) {
      const size_t size = make(raw, Data, id, epoch, ++transmit, payload, n);
      if (size)
        result = HelmetLink::cobsEncode(raw, size, out, capacity);
    }
    protogenSecureZero(payload, sizeof(payload));
    protogenSecureZero(raw, sizeof(raw));
    return result;
  }
  bool feed(uint8_t byte, uint32_t now, HelmetLink::Packet &p) {
    advanceClock(now);
    if (used && now - frameAt > 100) {
      used = 0;
      discarding = true;
    }
    if (byte) {
      if (!used)
        frameAt = now;
      if (discarding)
        return false;
      if (used == sizeof(encoded)) {
        used = 0;
        discarding = true;
        if (errors != UINT32_MAX)
          ++errors;
        return false;
      }
      encoded[used++] = byte;
      return false;
    }
    if (discarding) {
      discarding = false;
      used = 0;
      return false;
    }
    uint8_t raw[MaxRaw]{};
    const size_t n = HelmetLink::cobsDecode(encoded, used, raw, sizeof(raw));
    used = 0;
    handledControl = false;
    const bool okay = n && decode(raw, n, now, p);
    protogenSecureZero(raw, sizeof(raw));
    if (!okay && !handledControl && errors != UINT32_MAX)
      ++errors;
    return okay;
  }
};
} // namespace UartSecurity
