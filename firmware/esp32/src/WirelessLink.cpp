#include "WirelessLink.h"
#include <string.h>
namespace Wireless {
bool Link::begin(bool isPaired, const uint8_t secret[32], uint32_t now) {
  if (!Crypto::init())
    return false;
  paired = isPaired;
  if (paired)
    memcpy(psk, secret, 32);
  started = true;
  recover(now);
  return started;
}
void Link::recover(uint32_t now) {
  const bool wasConnected = connected;
  connected = false;
  session.clear();
  handshake.clear();
  txPending = false;
  rotateQueued = rotateSent = false;
  count = readAt = writeAt = 0;
  if (wasConnected)
    hooks.connection(hooks.context, false);
  if (!randomIdentity(own, role) ||
      !radio.configure(own, nullptr, nullptr, nullptr, PairChannel)) {
    started = false;
    return;
  }
  recoveryAt = identityAt = now;
  fallback = !haveRecovery;
  recovering = paired;
  if (paired && (!recovery.start(haveRecovery ? recoveryKey : psk, role, now) ||
                 !handshake.start(psk, role, own, now)))
    started = false;
  currentChannel = PairChannel;
  lastBusy = now;
}
bool Link::startPairing(uint32_t now) {
  if (!started)
    return false;
  if (connected)
    hooks.connection(hooks.context, false);
  connected = recovering = false;
  session.clear();
  handshake.clear();
  count = readAt = writeAt = 0;
  txPending = false;
  if (!randomIdentity(own, role) ||
      !radio.configure(own, nullptr, nullptr, nullptr, PairChannel))
    return false;
  currentChannel = PairChannel;
  return pairing.start(role, own, now);
}
bool Link::establish(uint32_t now) {
  const Material &m = handshake.keys();
  own = role == Paw ? m.pawMac : m.helmetMac;
  peer = role == Paw ? m.helmetMac : m.pawMac;
  if (!radio.configure(own, &peer, m.lmk, m.pmk, currentChannel))
    return false;
  session.begin(role, m);
  memcpy(recoveryKey, m.recovery, 32);
  haveRecovery = true;
  handshake.clear();
  recovering = false;
  connected = true;
  lastAuthenticated = now;
  uint32_t jitter = 0;
  Crypto::random(&jitter, sizeof(jitter));
  renewAt = now + SessionLifetimeMs + jitter % SessionJitterMs;
  rotateQueued = rotateSent = false;
  nextKeepalive = now;
  activeUntil = now + ActiveMs;
  nextPoll = now;
  hooks.connection(hooks.context, true);
  return true;
}
bool Link::queue(const HelmetLink::Packet &p) {
  if (!connected || count == TxSlots || !applicationValid(p, role))
    return false;
  outgoing[writeAt] = p;
  writeAt = (writeAt + 1) % TxSlots;
  ++count;
  return true;
}
void Link::keepalive(uint32_t now) {
  if (role != Paw)
    return;
  const bool active =
      !due(now, activeUntil) || now - lastAuthenticated > KeepaliveMaxMs + 1000;
  if ((active && due(now, nextPoll)) || due(now, nextKeepalive)) {
    HelmetLink::Packet request;
    request.kind = HelmetLink::Status;
    if (queue(request)) {
      uint32_t jitter = 0;
      Crypto::random(&jitter, sizeof(jitter));
      nextKeepalive =
          now + KeepaliveMinMs + jitter % (KeepaliveMaxMs - KeepaliveMinMs + 1);
      nextPoll = now + ActivePollMs;
    }
  }
}
void Link::tick(uint32_t now) {
  if (!started)
    return;
  if (!radio.busy())
    lastBusy = now;
  if (radio.busy() && now - lastBusy >= HandshakeMs) {
    recover(now);
    return;
  } // Driver completion failure cannot wedge the link.
  Received item;
  for (unsigned budget = 0; budget < ReceiveBudget && radio.receive(item);
       ++budget) {
    if (!valid(item.frame.bytes, PacketBytes, role == Paw ? Helmet : Paw))
      continue;
    if (pairing.active()) {
      pairing.receive(item.frame, item.source, now);
      continue;
    }
    if (connected) {
      if (item.broadcast || !same(item.source, peer) ||
          now - lastRxCheck < AuthIntervalMs / 10)
        continue;
      lastRxCheck = now;
      HelmetLink::Packet p;
      if (session.open(item.frame.bytes, PacketBytes, p)) {
        lastAuthenticated = now;
        if (role == Helmet && p.kind == HelmetLink::Status && p.length == 1 &&
            p.payload[0] == 1) {
          recover(now);
          return;
        }
        hooks.packet(hooks.context, p);
      }
    } else if (recovering)
      handshake.receive(item.frame, item.source, now);
  }
  if (pairing.active()) {
    if (pairing.ready()) {
      uint8_t candidate[32];
      if (pairing.getSecret(candidate) &&
          hooks.store(hooks.context, candidate)) {
        memcpy(psk, candidate, 32);
        paired = true;
        haveRecovery = false;
        pairing.stored(now);
      } else {
        pairing.clear();
        recover(now);
      }
      Crypto::wipe(candidate, sizeof(candidate));
    }
    Frame frame;
    Mac destination;
    bool broadcast = false;
    if (!radio.busy() && pairing.tick(now, frame, destination, broadcast))
      radio.send(frame, broadcast ? nullptr : &destination);
    if (!pairing.active())
      recover(now);
    return;
  }
  if (connected) {
    if (now - lastAuthenticated >= DisconnectMs) {
      recover(now);
      return;
    }
    if (rotateSent && !txPending && !radio.busy()) {
      recover(now);
      return;
    }
    if (role == Paw && due(now, renewAt) && due(now, activeUntil) && !count &&
        !txPending && !rotateQueued) {
      HelmetLink::Packet rekey;
      rekey.kind = HelmetLink::Status;
      rekey.length = 1;
      rekey.payload[0] = 1;
      rotateQueued = queue(rekey);
    }
    if (!rotateQueued)
      keepalive(now);
    if (!txPending && count) {
      const bool rekey = outgoing[readAt].kind == HelmetLink::Status &&
                         outgoing[readAt].length == 1;
      if (rekey)
        rotateSent = true;
      txPending = session.seal(outgoing[readAt], tx);
      readAt = (readAt + 1) % TxSlots;
      --count;
      if (!txPending) {
        recover(now);
        return;
      }
    }
    if (txPending && !radio.busy() && radio.send(tx, &peer))
      txPending = false;
    return;
  }
  if (!recovering)
    return;
  if (haveRecovery && !fallback && now - recoveryAt >= FastRecoveryMs) {
    fallback = true;
    recovery.start(psk, role, now);
    handshake.start(psk, role, own, now);
  }
  if (handshake.ready()) {
    if (!radio.busy() && !establish(now))
      recover(now);
    return;
  }
  if (!radio.busy() &&
      (now - identityAt >= RecoveryIdentityMs + HandshakeMs ||
       (now - identityAt >= RecoveryIdentityMs && !handshake.pending()))) {
    if (!randomIdentity(own, role) ||
        !radio.configure(own, nullptr, nullptr, nullptr, currentChannel) ||
        !handshake.start(psk, role, own, now)) {
      started = false;
      return;
    }
    identityAt = lastBusy = now;
  }
  if (!handshake.pending()) {
    const uint8_t channel = recovery.channel(now);
    if (channel != currentChannel && radio.channel(channel))
      currentChannel = channel;
  }
  Frame frame;
  Mac destination;
  bool broadcast = false;
  if (!radio.busy() && handshake.tick(now, frame, destination, broadcast) &&
      radio.send(frame, broadcast ? nullptr : &destination))
    handshake.sent(frame);
}
} // namespace Wireless
