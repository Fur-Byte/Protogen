#include "WirelessLink.h"
#include <assert.h>
#include <string.h>
namespace Wireless {
void Link::transition(State next) {
  // Physical pairing and recovery may interrupt every state, after keys clear.
  const bool allowed =
      next == phase || next == State::Recovering || next == State::Unpaired ||
      next == State::PairedOffline || next == State::PairingDiscovery ||
      (phase == State::PairingDiscovery &&
       next == State::PairingConfirmation) ||
      (recovering() &&
       (next == State::Rendezvous || next == State::AuthenticatedEcdh ||
        next == State::KeyConfirmation)) ||
      (phase == State::KeyConfirmation && next == State::Online) ||
      (phase == State::Online && next == State::Rekeying);
  assert(allowed);
  if (!allowed) {
    session.clear();
    handshake.clear();
    phase = paired ? State::PairedOffline : State::Unpaired;
    return;
  }
  phase = next;
}

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
  const bool wasConnected = connected();
  session.clear();
  handshake.clear();
  pairing.clear();
  transition(paired ? State::Recovering : State::Unpaired);
  increment(budget.diagnostics.recoveries);
  if (wasConnected)
    increment(budget.diagnostics.disconnects);
  Crypto::wipe(outgoing, sizeof(outgoing));
  Crypto::wipe(&tx, sizeof(tx));
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
  if (paired) {
    if (!recovery.start(haveRecovery ? recoveryKey : psk, role, now)) {
      started = false;
      transition(State::PairedOffline);
      return;
    }
    handshake.start(psk, role, own, now);
    retryAt = now + InvalidBackoffMs;
    transition(State::Rendezvous);
  }
  increment(budget.diagnostics.identityRotations);
  currentChannel = PairChannel;
  lastBusy = now;
}
bool Link::startPairing(uint32_t now) {
  if (!started)
    return false;
  if (connected())
    hooks.connection(hooks.context, false);
  session.clear();
  handshake.clear();
  pairing.clear();
  transition(paired ? State::Recovering : State::Unpaired);
  count = readAt = writeAt = 0;
  txPending = false;
  if (!randomIdentity(own, role) ||
      !radio.configure(own, nullptr, nullptr, nullptr, PairChannel)) {
    recover(now);
    return false;
  }
  currentChannel = PairChannel;
  Crypto::wipe(outgoing, sizeof(outgoing));
  Crypto::wipe(&tx, sizeof(tx));
  if (!pairing.start(role, own, now)) {
    recover(now);
    return false;
  }
  transition(State::PairingDiscovery);
  return true;
}
bool Link::establish(uint32_t now) {
  if (!paired || pairing.active() || !handshake.ready() ||
      phase != State::KeyConfirmation)
    return false;
  const Material &m = handshake.keys();
  own = role == Paw ? m.pawMac : m.helmetMac;
  peer = role == Paw ? m.helmetMac : m.pawMac;
  if (!radio.configure(own, &peer, m.lmk, m.pmk, currentChannel))
    return false;
  if (!handshake.ready() || phase != State::KeyConfirmation ||
      !session.begin(role, m))
    return false;
  memcpy(recoveryKey, m.recovery, 32);
  haveRecovery = true;
  handshake.clear();
  transition(State::Online);
  sessionAt = now;
  increment(budget.diagnostics.identityRotations);
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
  if (!online() || count == TxSlots || !applicationValid(p, role))
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
  assert(invariant());
  if (!radio.busy())
    lastBusy = now;
  if (radio.busy() && now - lastBusy >= HandshakeMs) {
    recover(now);
    return;
  } // Driver completion failure cannot wedge the link.
  Received item;
  for (unsigned received = 0; received < ReceiveBudget; ++received) {
    // Preserve queued records while the authentication budget cools down.
    // Dequeuing the whole burst here silently discarded legitimate ACK/status
    // records after the first attempted authentication in the same window.
    if(connected() && now-lastRxCheck<AuthIntervalMs/10)break;
    if(!radio.receive(item))break;
    if (!valid(item.frame.bytes, PacketBytes, role == Paw ? Helmet : Paw)) {
      increment(item.frame.bytes[1] != Version ? budget.diagnostics.versions
                                               : budget.diagnostics.malformed);
      continue;
    }
    if (pairing.active()) {
      pairing.receive(item.frame, item.source, now);
      if (!pairing.active()) {
        recover(now);
        return;
      }
      continue;
    }
    if (connected()) {
      if (!online())
        continue;
      // Public control packets cannot consume the active record-auth budget.
      if (item.broadcast || item.frame.bytes[2] != Data ||
          !same(item.source, peer) ||
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
    } else if (recovering()) {
      handshake.receive(item.frame, item.source, now);
      if (handshake.pending())
        transition(State::KeyConfirmation);
    }
  }
  if (pairing.active()) {
    if (pairing.awaitingConfirmation())
      transition(State::PairingConfirmation);
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
  if (connected()) {
    if (now - lastAuthenticated >= DisconnectMs) {
      recover(now);
      return;
    }
    if (rotateSent && !txPending && !radio.busy()) {
      recover(now);
      return;
    }
    if ((session.needsRekey() && (role == Helmet || count || txPending)) ||
        (role == Paw && due(now, renewAt + ActiveMs))) {
      increment(budget.diagnostics.renewals);
      recover(now);
      return;
    }
    if (role == Paw && (due(now, renewAt) || session.needsRekey()) &&
        (session.needsRekey() || due(now, activeUntil)) && !count &&
        !txPending && !rotateQueued) {
      HelmetLink::Packet rekey;
      rekey.kind = HelmetLink::Status;
      rekey.length = 1;
      rekey.payload[0] = 1;
      rotateQueued = queue(rekey);
      if (rotateQueued) {
        increment(budget.diagnostics.renewals);
        transition(State::Rekeying);
      }
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
  if (!recovering())
    return;
  if (handshake.failed() && due(now, retryAt)) {
    handshake.start(psk, role, own, now);
    retryAt = now + InvalidBackoffMs;
    transition(State::Rendezvous);
  }
  if (haveRecovery && !fallback && now - recoveryAt >= FastRecoveryMs) {
    fallback = true;
    Crypto::wipe(recoveryKey, 32);
    haveRecovery = false;
    recovery.start(psk, role, now);
    handshake.start(psk, role, own, now);
    transition(State::Rendezvous);
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
        !radio.configure(own, nullptr, nullptr, nullptr, currentChannel)) {
      started = false;
      return;
    }
    handshake.start(psk, role, own, now);
    retryAt = now + InvalidBackoffMs;
    transition(State::Rendezvous);
    increment(budget.diagnostics.identityRotations);
    identityAt = lastBusy = now;
  }
  if (!handshake.pending()) {
    const uint8_t channel = recovery.channel(now);
    if (channel != currentChannel && radio.channel(channel)) {
      currentChannel = channel;
      increment(budget.diagnostics.channelRecoveries);
    }
  }
  Frame frame;
  Mac destination;
  bool broadcast = false;
  if (!radio.busy() && handshake.tick(now, frame, destination, broadcast) &&
      radio.send(frame, broadcast ? nullptr : &destination))
    handshake.sent(frame);
}
} // namespace Wireless
