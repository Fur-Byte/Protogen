// Reuse the real production Link/PSA fixtures; no stand-in crypto or RF model
// beyond the repository's explicitly idealized, bounded FakeRadio.
#define main existing_wireless_regression_main
#include "wireless.cpp"
#undef main
#include <algorithm>

namespace {
Frame offer(Handshake &p, uint32_t now) {
  Frame f; Mac destination; bool broadcast=false;
  assert(p.tick(now,f,destination,broadcast) && broadcast);
  return f;
}
void requestCannotOwnCandidate() {
  uint8_t secret[32]={29}; Handshake p,h;
  assert(p.start(secret,Paw,paw,100) && h.start(secret,Helmet,helmet,100));
  const Frame captured=offer(p,100);
  assert(h.receive(captured,paw,100));
  std::cerr << "initial authenticated offer leaves expensive candidate free=" << !h.pending() << '\n';
  assert(!h.pending());
  assert(h.start(secret,Helmet,helmet,10000));
  for(unsigned i=0;i<20;++i) {
    h.receive(captured,paw,10000+i*100);
    assert(!h.pending());
  }
}
void badTagsCannotLockOutGood() {
  uint8_t secret[32]={29}; CryptoBudget budget; Handshake p,h(&budget);
  assert(p.start(secret,Paw,paw,20000) && h.start(secret,Helmet,helmet,20000));
  const Frame good=offer(p,20000); Frame bad=good; bad.bytes[127]^=1;
  for(unsigned i=0;i<AuthBurst;++i) assert(!h.receive(bad,paw,20000));
  const bool accepted=h.receive(good,paw,20001);
  std::cerr << "genuine offer admitted during bad-tag traffic=" << accepted << '\n';
  assert(accepted);
  uint32_t now=20001;Mac destination;bool broadcast=false;
  Frame challenge;assert(h.tick(now,challenge,destination,broadcast));
  Frame badChallenge=challenge;badChallenge.bytes[127]^=1;
  for(unsigned i=0;i<AuthBurst*2;++i)assert(!p.receive(badChallenge,helmet,now));
  assert(!p.pending() && p.receive(challenge,helmet,now));
  Frame proof;assert(p.tick(now,proof,destination,broadcast));
  assert(h.receive(proof,paw,now));
  Frame response;assert(h.tick(now,response,destination,broadcast));
  Frame badResponse=response;badResponse.bytes[127]^=1;
  for(unsigned i=0;i<AuthBurst*2;++i)assert(!p.receive(badResponse,helmet,now));
  assert(p.receive(response,helmet,now));
}
void invalidFinishedPreservesCandidate() {
  uint8_t secret[32]={29}; Handshake p,h;
  assert(p.start(secret,Paw,paw,100) && h.start(secret,Helmet,helmet,100));
  Frame f,finish;Mac destination;bool broadcast=false,found=false;
  for(uint32_t now=100;now<3000 && !found;now+=5) {
    if(p.tick(now,f,destination,broadcast)) {
      if(f.bytes[2]==SessionFinish) {finish=f;found=true;break;}
      h.receive(f,paw,now);
    }
    if(h.tick(now,f,destination,broadcast))p.receive(f,helmet,now);
  }
  assert(found && h.pending());
  Frame bad=finish;bad.bytes[127]^=1;
  assert(!h.receive(bad,paw,200));
  std::cerr << "invalid Finished leaves authenticated candidate usable=" << !h.failed() << '\n';
  assert(!h.failed() && h.pending());
  assert(h.receive(finish,paw,201));
}
struct Recording { Frame request,challenge,proof,response; };
Frame nextType(Handshake &from,Type type,uint32_t &now) {
  Frame f;Mac destination;bool broadcast=false;
  for(unsigned tries=0;tries<HandshakeRetryMs+20;++tries,++now)
    if(from.tick(now,f,destination,broadcast) && f.bytes[2]==type)return f;
  assert(false && "expected bounded control transmission");return f;
}
Recording challengeAndProof(Handshake &p,Handshake &h,uint32_t &now) {
  Recording r;r.request=nextType(p,SessionRequest,now);
  assert(h.receive(r.request,paw,now));
  r.challenge=nextType(h,SessionChallenge,now);
  assert(p.receive(r.challenge,helmet,now));
  r.proof=nextType(p,SessionProof,now);
  return r;
}
void completeExchange(Handshake &p,Handshake &h,uint32_t &now) {
  Frame f;Mac destination;bool broadcast=false;
  for(unsigned i=0;i<1000 && !(p.ready()&&h.ready());++i,++now) {
    if(p.tick(now,f,destination,broadcast)) {h.receive(f,paw,now);p.sent(f);}
    if(h.tick(now,f,destination,broadcast)) {p.receive(f,helmet,now);h.sent(f);}
  }
  assert(p.ready()&&h.ready());
  assert(Crypto::equal(p.keys().pawKey,h.keys().pawKey,32));
  assert(Crypto::equal(p.keys().helmetKey,h.keys().helmetKey,32));
}
void capturedProofAndChallengeCannotReturn() {
  uint8_t secret[32]={29};Handshake p,h;
  uint32_t now=100;
  assert(p.start(secret,Paw,paw,now)&&h.start(secret,Helmet,helmet,now));
  auto recording=challengeAndProof(p,h,now);
  assert(h.receive(recording.proof,paw,now));
  recording.response=nextType(h,SessionResponse,now);
  assert(p.receive(recording.response,helmet,now));
  completeExchange(p,h,now);
  for(uint32_t restart : {10000u,40000u,UINT32_MAX-1000u}) {
    Handshake freshPaw;now=restart;
    assert(h.start(secret,Helmet,helmet,now));
    assert(freshPaw.start(secret,Paw,paw,now));
    assert(!h.receive(recording.proof,paw,now));
    assert(!h.pending());
    assert(!freshPaw.receive(recording.challenge,helmet,now));
    assert(!freshPaw.pending());
    // An authentic old request may receive a cheap reply; never owns a slot.
    assert(h.receive(recording.request,paw,now));assert(!h.pending());
    completeExchange(freshPaw,h,now);
    assert(!h.receive(recording.proof,paw,now));
    assert(!Crypto::equal(freshPaw.keys().pawKey,p.keys().pawKey,32));
  }
}
void freshProofSupersedesAndReservationIsAtomic() {
  uint8_t secret[32]={29};CryptoBudget budget;Handshake h(&budget),oldP,newP;
  uint32_t now=100;
  assert(h.start(secret,Helmet,helmet,now)&&oldP.start(secret,Paw,paw,now));
  auto old=challengeAndProof(oldP,h,now);
  assert(h.receive(old.proof,paw,now));
  old.response=nextType(h,SessionResponse,now);
  Material original=h.keys();
  ++now;assert(newP.start(secret,Paw,paw,now));
  auto fresh=challengeAndProof(newP,h,now);
  assert(budget.spend(budget.ecc,now)); // leave exactly one credit
  assert(!h.receive(fresh.proof,paw,now));
  assert(Crypto::equal(&original,&h.keys(),sizeof(original)));
  assert(budget.spend(budget.ecc,now)); // failed pair reservation spent zero
  now+=1000;
  assert(h.receive(fresh.proof,paw,now)); // same nonce/proof still eligible
  assert(!h.receive(old.proof,paw,now));
  fresh.response=nextType(h,SessionResponse,now);
  assert(newP.receive(fresh.response,helmet,now));
  const auto committed=h.keys();
  for(unsigned i=0;i<100;++i)assert(h.receive(fresh.proof,paw,now));
  assert(Crypto::equal(&committed,&h.keys(),sizeof(committed)));
  completeExchange(newP,h,now);
  Crypto::wipe(&original,sizeof(original));
}
Frame badRequest(const Frame &sample,const Mac &source) {
  Frame f=sample;memcpy(f.bytes+85,source.bytes,6);f.bytes[127]^=1;return f;
}
void notificationWorkIsBounded() {
  Rig r;uint8_t secret[32]={9};Handshake p;assert(p.start(secret,Paw,paw,100));
  const auto bad=badRequest(offer(p,100),paw);
  for(unsigned i=0;i<8;++i)r.h.radio.incoming.push_back({bad,paw,true});
  for(unsigned i=0;i<1000;++i)r.h.link->tick(r.now);
  assert(r.h.radio.incoming.size()==4);
  assert(r.h.link->securityStatus(r.now).counters.handshakeFailures==4);
  r.h.link->tick(r.now+1);assert(r.h.radio.incoming.size()==4);
  r.h.link->tick(r.now+2);assert(r.h.radio.incoming.empty());
}
void sustainedFloodHasLegitimateDeliveryOpportunity() {
  uint32_t worst=0;unsigned trials=0;
  for(bool atPaw : {false,true})for(bool spoof : {false,true})
  for(unsigned pattern=0;pattern<3;++pattern)for(unsigned phase : {0u,5u,100u,245u}) {
    Rig r;Handshake source;uint8_t secret[32]={9};
    assert(source.start(secret,Paw,paw,100));const Frame sample=offer(source,100);
    r.p.radio.blocked=true;
    const uint32_t warm=12000,limit=10000;
    bool connected=false;
    for(uint32_t t=0;t<warm+limit;t+=5) {
      if(t==warm)r.p.radio.blocked=false;
      const unsigned period=pattern==0?25u:pattern==1?250u:1000u;
      const unsigned count=pattern==0?1u:8u;
      if(t>=phase && (t-phase)%period==0) {
        for(unsigned k=0;k<count;++k) {
          Mac src=spoof?(atPaw?r.h.radio.own:r.p.radio.own):
            Mac{{2,9,uint8_t(t),uint8_t(t>>8),uint8_t(k),uint8_t(t/25)}};
          Frame bad=badRequest(sample,src);
          if(atPaw) {
            // A validly structured but unauthenticated Challenge, bound to the
            // current request where available; it must not alter paw state.
            for(auto it=r.p.radio.captured.rbegin();it!=r.p.radio.captured.rend();++it)
              if(it->bytes[2]==SessionRequest) {
                header(bad,SessionChallenge,Helmet);
                assert(Crypto::hash(it->bytes,96,bad.bytes+4));
                bad.bytes[36]=1;memcpy(bad.bytes+52,src.bytes,6);
                bad.bytes[58]=HandshakeRevision;bad.bytes[127]=1;break;
              }
          }
          auto &q=atPaw?r.p.radio.incoming:r.h.radio.incoming;
          if(q.size()<RxSlots)q.push_back({bad,src,!atPaw});
        }
      }
      r.step(5);
      if(t>=warm && r.online()) {
        const auto elapsed=t+5-warm;if(elapsed>worst)worst=elapsed;
        connected=true;break;
      }
    }
    assert(connected);++trials;
    assert(r.p.link->queue(command(50000+trials)));const auto before=r.h.actions;
    r.step(100);assert(r.h.actions==before+1);
  }
  std::cout<<"PASS: "<<trials<<" real-crypto/model-radio reconnects DURING bounded8–40/s traffic; worst_ms="<<worst<<" (no RF/queue-saturation guarantee)\n";
}
void genuineCapturedRequestsCannotOwnHandshake() {
  std::array<Frame,4> captured;uint8_t secret[32]={9};
  for(unsigned i=0;i<captured.size();++i) {
    Handshake sender;Mac own{{2,44,33,22,11,uint8_t(i+1)}};
    assert(sender.start(secret,Paw,own,100));captured[i]=offer(sender,100);
  }
  uint32_t worst=0;unsigned trials=0;
  for(unsigned phase : {0u,5u,100u,245u})for(unsigned pool : {1u,2u,4u}) {
    Rig r;r.p.radio.blocked=true;bool connected=false;
    for(uint32_t t=0;t<30000;t+=5) {
      if(t==20000)r.p.radio.blocked=false;
      if(t>=phase&&(t-phase)%25==0) {
        const Frame &f=captured[(t/25)%pool];Mac source;memcpy(source.bytes,f.bytes+85,6);
        if(r.h.radio.incoming.size()<RxSlots)r.h.radio.incoming.push_back({f,source,true});
      }
      r.step(5);
      if(t<20000) {
        const auto state=r.h.link->securityStatus(r.now).state;
        assert(state!=State::KeyConfirmation && state!=State::AuthenticatedEcdh);
      } else if(r.online()) {
        const auto elapsed=t+5-20000;if(elapsed>worst)worst=elapsed;
        connected=true;break;
      }
    }
    assert(connected);++trials;
    const auto before=r.h.actions;assert(r.p.link->queue(command(70000+trials)));
    r.step(100);assert(r.h.actions==before+1);
  }
  std::cout<<"PASS: "<<trials<<" reconnects during continuous40/s replay of1/2/4 genuine captured requests; no expensive slot before fresh proof; worst_ms="<<worst<<'\n';
}
void lostControlFramesRecover() {
  for(Type type : {SessionRequest,SessionChallenge,SessionProof,SessionResponse,SessionFinish,SessionCommit}) {
    Rig r;
    auto &sender=(type==SessionChallenge||type==SessionResponse)?r.h.radio:r.p.radio;
    sender.dropControl[type]=1;
    r.connect();assert(!sender.dropControl[type]);
    const auto before=r.h.actions;assert(r.p.link->queue(command(80000+type)));
    r.step(100);assert(r.h.actions==before+1);
  }
  std::cout<<"PASS: each of6 control message types lost once, bounded retry/recovery and legitimate encrypted command\n";
}
void challengeQueueBackpressurePreservesDeliveredRequest() {
  uint32_t worst=0;
  for(unsigned offset : {0u,2u,4u,6u,8u,10u}) {
    Rig r;uint8_t secret[32]={9};Frame captured[4];Mac old{{2,33,44,55,66,77}};
    for(auto &f:captured) {
      Handshake p;assert(p.start(secret,Paw,old,100));f=offer(p,100);
    }
    bool connected=false;
    // Independent review reproduction: four authentic old requests fill the
    // cheap reply FIFO, then a fifth one competes with a delivered live offer.
    // Only20 replay packets/s and no RX overflow: progress must take <4s.
    for(uint32_t elapsed=0;elapsed<4000;elapsed+=2) {
      r.now=100+elapsed;
      if(elapsed>=offset && (elapsed-offset)%250==0)
        for(const auto &f:captured)r.h.radio.incoming.push_back({f,old,true});
      if(elapsed>=offset && (elapsed-offset)%250==2)
        r.h.radio.incoming.push_back({captured[0],old,true});
      if(elapsed)r.p.link->tick(r.now);
      assert(r.h.radio.incoming.size()<=RxSlots);
      r.h.link->tick(r.now);
      if(r.online()) {worst=std::max(worst,elapsed);connected=true;break;}
    }
    assert(connected && "delivered request lost behind full challenge-reply FIFO");
    const auto before=r.h.actions;assert(r.p.link->queue(command(90000+offset)));
    r.step(100);assert(r.h.actions==before+1);
  }
  std::cout<<"PASS:6 paced20/s captured-request FIFO phases, no RX overflow, reconnect worst_ms="<<worst<<'\n';
}
void legacyIsExplicitlyRejected() {
  uint8_t secret[32]={29};Handshake p,h;assert(p.start(secret,Paw,paw,100)&&h.start(secret,Helmet,helmet,100));
  Frame legacy=offer(p,100);legacy.bytes[2]=SessionHello;legacy.bytes[91]=0;
  uint8_t oldKey[32];assert(Crypto::hkdf(secret,32,nullptr,0,"protogen/v3/session/auth/hello",oldKey,32));
  assert(Crypto::hmac(oldKey,legacy.bytes,96,legacy.bytes+96));
  assert(!h.receive(legacy,paw,100)&&!h.pending());
  Frame request=offer(p,400);
  assert(request.bytes[1]==3&&request.bytes[2]>8&&request.bytes[91]==2);
  // The supported legacy parser's allowed type range ended at8. No automatic
  // fallback to that unauthenticated-freshness handshake is implemented.
  assert(!(request.bytes[2]>=1&&request.bytes[2]<=8));
  Frame future=request;future.bytes[1]=4;
  assert(!h.receive(future,paw,400));
  Crypto::wipe(oldKey,sizeof(oldKey));
}
void completedHandshakeRetiresAdmissionSecrets() {
  uint8_t secret[32]={29};Handshake p,h;uint32_t now=100;
  assert(p.start(secret,Paw,paw,now)&&h.start(secret,Helmet,helmet,now));
  completeExchange(p,h,now);
  for(const auto *label : {"request","challenge","proof","response"}) {
    const std::string domain=std::string("protogen/v3/handshake/r2/")+label;
    uint8_t key[32];assert(Crypto::hkdf(secret,32,nullptr,0,domain.c_str(),key,32));
    for(const auto *object : {&p,&h}) {
      const auto *begin=reinterpret_cast<const uint8_t*>(object);
      assert(std::search(begin,begin+sizeof(*object),key,key+32)==begin+sizeof(*object));
    }
    Crypto::wipe(key,sizeof(key));
  }
}
}
int main(int argc,char **argv) {
  assert(Crypto::init());
  const std::string mode=argc==2?argv[1]:"all";
  if(mode=="all"||mode=="request")requestCannotOwnCandidate();
  if(mode=="all"||mode=="budget")badTagsCannotLockOutGood();
  if(mode=="all"||mode=="finished")invalidFinishedPreservesCandidate();
  if(mode=="all"||mode=="freshness") {capturedProofAndChallengeCannotReturn();freshProofSupersedesAndReservationIsAtomic();legacyIsExplicitlyRejected();lostControlFramesRecover();}
  if(mode=="all"||mode=="flood") {notificationWorkIsBounded();sustainedFloodHasLegitimateDeliveryOpportunity();genuineCapturedRequestsCannotOwnHandshake();}
  if(mode=="all"||mode=="queue")challengeQueueBackpressurePreservesDeliveredRequest();
  if(mode=="all"||mode=="wipe")completedHandshakeRetiresAdmissionSecrets();
  assert(mode=="all"||mode=="request"||mode=="budget"||mode=="finished"||mode=="freshness"||mode=="flood"||mode=="wipe"||mode=="queue");
  std::cout << "PASS: fresh admission, valid progress and pending confirmation preservation\n";
}
