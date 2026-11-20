#include "sketch.h"
#include "WirelessConfig.h"
#ifdef PROTOGEN_UART_AUTH
#include "src/UartCrypto/UartMac.h"
UartSecurity::MbedMac bridgeMac;
bool bridgeAuth(void *,const uint8_t *b,size_t n,uint8_t t[32]) {return bridgeMac.authenticate(b,n,t);}
bool bridgeRandom(void *,uint8_t *b,size_t n) {while(n--)*b++=static_cast<uint8_t>(Entropy.random());return true;}
UartSecurity::Endpoint bridge(UartSecurity::Esp,nullptr,bridgeAuth,bridgeRandom);
#else
HelmetLink::Decoder bridge;
#endif
using HelmetLink::Packet;
Packet latest;
uint32_t sequence=0;
void collect() {
  Packet p;
  for(uint8_t b:Serial1.output)if(bridge.feed(b,millis(),p)&&p.kind==HelmetLink::Status)latest=p;
  Serial1.output.clear();
}
void step(uint32_t us=1000) {advanceTime(us);loop();collect();}
void run(uint32_t ms) {for(uint32_t i=0;i<ms;++i)step();}
size_t encode(const Packet &p,uint8_t *out,size_t capacity) {
#ifdef PROTOGEN_UART_AUTH
  return bridge.seal(p,out,capacity);
#else
  uint8_t raw[HelmetLink::MaxRaw];const size_t n=HelmetLink::pack(p,raw,sizeof(raw));
  return HelmetLink::cobsEncode(raw,n,out,capacity);
#endif
}
// Wire delivery uses ceil(10 bits /115200) =87 us per byte. Actual target
// ISR/CPU/radio durations are not measured by this host scheduler.
void wire(const uint8_t *b,size_t n) {for(size_t i=0;i<n;++i){advanceTime(87);Serial1.input.push_back(char(b[i]));loop();collect();}}
void send(Packet p,bool noise=false) {
  uint8_t encoded[256];const size_t n=encode(p,encoded,sizeof(encoded));assert(n);
  if(noise){uint8_t junk[128];memset(junk,0x7f,sizeof(junk));junk[127]=0;wire(junk,sizeof(junk));}
  wire(encoded,n);
}
Packet command(uint8_t action,uint8_t value) {
  Packet p;p.kind=HelmetLink::Command;p.epoch=latest.epoch;p.ticket=latest.ticket;
  p.sequence=++sequence;p.length=8;HelmetLink::put32(p.payload,100);
  p.payload[4]=action;p.payload[5]=value;HelmetLink::put16(p.payload+6,action==HelmetLink::Boop?1200:0);return p;
}
int main() {
  // Three stages keep the command pending through completion, independent of
  // whether the first visible closure still belongs to the previous expression.
  ResponseTiming timing;timing.start(7,UINT32_MAX-100,ResponseTiming::Uart,ResponseTiming::Expression,0,1);
  timing.observe(200,true,false,false);assert(timing.feedbackUs==301&&timing.pending);
  timing.observe(300,false,true,true);assert(timing.completed==1&&!timing.pending);
  timing.start(8,300,ResponseTiming::Uart,ResponseTiming::Expression,0,1);
  timing.start(9,400,ResponseTiming::Uart,ResponseTiming::Expression,0,2);assert(timing.superseded==1);
  timing.poll(5000400);assert(timing.expired==1&&!timing.pending);
  timing.accepted=UINT32_MAX;timing.start(10,2,ResponseTiming::Local,ResponseTiming::Expression,0,2);assert(timing.accepted==UINT32_MAX);
  setup();run(200);
#ifdef PROTOGEN_UART_AUTH
  assert(bridge.begin(0,millis()));
  for(unsigned i=0;i<1000&&!bridge.online();++i){uint8_t b[256];const size_t n=bridge.poll(millis(),b,sizeof(b));if(n)wire(b,n);step();}
  assert(bridge.online());
#endif
  run(120);assert(latest.epoch&&latest.ticket);
  Packet hello;hello.kind=HelmetLink::Hello;hello.epoch=latest.epoch;hello.ticket=latest.ticket;hello.length=4;HelmetLink::put32(hello.payload,100);send(hello);run(120);
  config.naturalBlinks=0;uint32_t maximum=0,maxOnset=0,maxCompletion=0;
  // The action starts at boop press or navigation release. Before UART, the
  // fixture charges 30 ms for debounce, four 2 ms C3 service opportunities,
  // and three 1 ms RF deliveries after idle (status request, reply, command).
  constexpr uint32_t upstreamUs=(Wireless::DebounceMs+4*Wireless::ServicePollMs+3)*1000;
  for(uint16_t duration:{uint16_t(Settings::TransitionMs),uint16_t(1000)})
  for(unsigned source=0;source<ExpressionCount;++source)for(unsigned mode=0;mode<4;++mode) {
    sleeping=false;visorVisible=true;config.transitionMs=0;selectExpression(source,millis());run(120);
    config.transitionMs=duration;
    const bool waking=mode==1,heavy=mode==2;
    if(waking){sleeping=true;step();assert(SPI.shutdown);}
    if(heavy){Serial.capacity=0;Serial.input="STATUS\n";step();}
    const auto before=SPI.display;const bool hidden=SPI.shutdown;
    const uint64_t edge=fakeUs;
    run(upstreamUs/1000);
    const uint8_t action=waking?HelmetLink::Sleep:mode==3?HelmetLink::Boop:HelmetLink::Expression;
    Packet p=command(action,(waking||action==HelmetLink::Boop)?0:(source+1)%ExpressionCount);
    send(p,heavy);
    uint64_t visible=0;
    for(unsigned i=0;i<220;++i){
      if(SPI.shutdown!=hidden||(!SPI.shutdown&&SPI.display!=before)){visible=fakeUs;break;}
      step();
    }
    if(mode==3&&source==ExpressionVwv) {
      assert(boopActive&&SPI.display==before);boopActive=boopFromRemote=false;
      Serial.capacity=4096;Serial.input.clear();usbReplySent=usbReplyLength;
      std::cout<<"model unchanged_vwv_boop=1 (no required pixel change)"<<std::endl;continue;
    }
    if(!visible)std::cerr<<"NO RESPONSE source="<<source<<" mode="<<mode<<" state="<<unsigned(state)<<" rejected="<<PawLink::gate.rejected<<" bridge="<<PawLink::gate.bridge<<" ready="<<connectorReady<<" boop="<<boopActive<<" hidden="<<SPI.shutdown<<" result="<<unsigned(PawLink::lastResult)<<std::endl;
    assert(visible);const uint32_t elapsed=static_cast<uint32_t>(visible-edge);if(elapsed>maximum)maximum=elapsed;
    std::cout<<"model transition_ms="<<duration<<" source="<<source<<" mode="<<mode<<" first_us="<<elapsed<<std::endl;
    assert(elapsed<100000);
    run(duration+20); // Complete the selected animation independently of first reaction.
    if(action==HelmetLink::Expression) {
      assert(!transitionActive&&state==p.payload[5]);
      assert(responseTiming.sequence==p.sequence&&responseTiming.origin==ResponseTiming::Uart);
      const uint32_t dispatchOffset=responseTiming.at-static_cast<uint32_t>(edge);
      assert(responseTiming.onset&&responseTiming.status==ResponseTiming::Complete);
      const uint32_t targetUs=dispatchOffset+responseTiming.onsetUs;
      const uint32_t completeUs=dispatchOffset+responseTiming.completionUs;
      maxOnset=std::max(maxOnset,targetUs);maxCompletion=std::max(maxCompletion,completeUs);
      std::cout<<"causal_model transition_ms="<<duration<<" source="<<source<<" mode="<<mode
        <<" command="<<p.sequence<<" feedback_us="<<dispatchOffset+responseTiming.feedbackUs
        <<" onset_us="<<targetUs<<" completion_us="<<completeUs<<std::endl;
      if(Settings::EarlyExpressionReveal&&!heavy)assert(targetUs<100000);
    }
    boopActive=boopFromRemote=false;Serial.capacity=4096;Serial.input.clear();
    usbReplySent=usbReplyLength;usbLineLength=0;usbOverflow=false;
  }
  std::cout<<"PASS: modeled physical-action to first visible max_us="<<maximum<<"; first feedback only; target onset and completion are separate diagnostics; actual UART/auth/renderer,4kHz sensor activity, wake and bounded traffic; target RF/CPU excluded\n";
  std::cout<<"model max_target_onset_us="<<maxOnset<<" max_completion_us="<<maxCompletion
    <<" early_reveal="<<Settings::EarlyExpressionReveal<<"; simulated clocks/SPI/UART, no RF/physical qualification\n";
  assert(responseTiming.completed>0&&responseTiming.maxFeedbackUs<100000);
  char diagnostic[]="LATENCY";handleUsbCommand(diagnostic,millis());
  assert(strstr(usbReply,"teensy_dispatch_to_causal_spi")&&strstr(usbReply,"\"ok\":true"));
  delete sensorAdc;
}
