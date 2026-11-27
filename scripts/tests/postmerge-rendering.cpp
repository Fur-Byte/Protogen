#include "sketch.h"
#include <string>
static void run(uint32_t ms) { for(uint32_t i=0;i<ms*4;++i) {advanceTime(250);loop();} }
static bool eyesEqual(const Face::Frame &a,const Face::Frame &b) {
  for(int y=0;y<8;++y)for(int x=0;x<16;++x)
    if(Face::pixel(a,x,y)!=Face::pixel(b,x,y)||Face::pixel(a,96+x,y)!=Face::pixel(b,96+x,y))return false;
  return true;
}
int main(int argc,char **argv) {
  assert(argc==2);setup();config.naturalBlinks=0;config.idleMotion=0;run(200);
  const std::string mode=argv[1];
  std::cout<<"mode early_reveal="<<Settings::EarlyExpressionReveal<<" scenario="<<mode<<std::endl;
  if(mode=="retarget") {
    unsigned trials=0;
    for(uint16_t duration:{0,160,1000})for(uint32_t gap:{0,1,10,30,50,90,159,320,950})
    for(uint8_t src=0;src<ExpressionCount;++src)for(uint8_t mid=0;mid<ExpressionCount;++mid)
    for(uint8_t dst=0;dst<ExpressionCount;++dst) {
      config.transitionMs=0;selectExpression(src,millis());run(20);
      config.transitionMs=duration;selectExpression(mid,millis());run(gap);
      const auto before=displayedFrame;const uint8_t emitted=displayedPose.expression,closure=displayedPose.closure;
      const bool changed=dst!=state;
      selectExpression(dst,millis());renderFace(millis());
      if(duration&&changed) {
        assert(displayedPose.closure==closure);
        if(!Settings::EarlyExpressionReveal)assert(eyesEqual(before,displayedFrame));
      }
      // Every actual emitted eye frame must belong to the last visible source
      // or newest target at its current closure, never an obsolete hidden mid.
      for(uint32_t t=0;t<uint32_t(duration)+20u;t+=4) {
        run(4);Face::Pose p=displayedPose;p.expression=emitted;Face::Frame old;Face::compose(old,p,Face::AudioSnapshot{});
        p.expression=dst;Face::Frame target;Face::compose(target,p,Face::AudioSnapshot{});
        assert(eyesEqual(displayedFrame,old)||eyesEqual(displayedFrame,target));
      }
      assert(state==dst&&!transitionActive);++trials;
    }
    std::cout<<"PASS: "<<trials<<" actual frame sequences, all expression triples incl repeats/reversals, 0/160/1000ms, nine gaps; continuous retarget closure\n";
  } else if(mode=="causal") {
    ResponseTiming timer;timer.start(7,UINT32_MAX-100,ResponseTiming::Uart,ResponseTiming::Expression,0,2);
    timer.observe(200,true,false,false);assert(timer.feedbackUs==301&&!timer.onset&&timer.pending);
    timer.observe(300,false,true,false);assert(timer.onsetUs==401&&timer.pending);
    timer.observe(400,false,false,true);assert(timer.completionUs==501&&timer.completed==1);
    timer.start(8,500,ResponseTiming::Usb,ResponseTiming::Expression,0,1);
    timer.skip(9,ResponseTiming::Local);assert(timer.pending&&timer.sequence==8&&timer.skippedSequence==9);
    timer.start(10,600,ResponseTiming::Uart,ResponseTiming::Expression,0,2);assert(timer.superseded==1);
    timer.delay();timer.delay();assert(timer.delayed==1);timer.poll(5000600);assert(timer.expired==1&&!timer.pending);
    config.transitionMs=160;selectExpression(0,millis());run(200);
    selectExpressionCommand(2,millis(),ResponseTiming::Uart,99,micros());run(30);
    assert(responseTiming.sequence==99&&responseTiming.feedback&&responseTiming.pending);
    assert(responseTiming.onset==Settings::EarlyExpressionReveal);
    run(180);assert(responseTiming.onset&&!responseTiming.pending&&responseTiming.completed);
    assert(responseTiming.onsetUs>=responseTiming.feedbackUs&&responseTiming.completionUs>=responseTiming.onsetUs);
    sleeping=true;loop();selectExpressionCommand(1,millis(),ResponseTiming::Usb,100,micros());run(100);
    assert(responseTiming.wasDelayed&&!responseTiming.feedback&&!responseTiming.onset&&responseTiming.pending);
    sleeping=false;run(20);assert(responseTiming.onset&&!responseTiming.pending&&responseTiming.sequence==100);
    boopActive=true;boopFromRemote=true;boopTime=millis();selectExpressionCommand(2,millis(),ResponseTiming::Uart,101,micros());run(30);
    counter=1;run(30);assert(!responseTiming.feedback&&!responseTiming.onset&&responseTiming.pending);
    boopActive=false;run(30);assert(responseTiming.onset&&!responseTiming.pending);
    // An unrelated visor safety event cannot satisfy sleep-command feedback.
    visorVisible=false;setSleepingCommand(true,ResponseTiming::Usb,102,micros());run(20);
    assert(responseTiming.pending&&!responseTiming.feedback&&responseTiming.wasDelayed);
    visorVisible=true;setSleepingCommand(false,ResponseTiming::Usb,103,micros());run(20);
    assert(!responseTiming.pending&&responseTiming.feedback&&!responseTiming.onset);
    assert(responseTiming.sequence==103&&responseTiming.supersededSequence==102);
    char diagnostic[]="LATENCY";handleUsbCommand(diagnostic,millis());
    assert(strstr(usbReply,"teensy_dispatch_to_causal_spi")&&strstr(usbReply,"target_onset_us"));
    // A sender sequence can restart in a new authenticated UART session. The
    // Teensy observation identity must still distinguish both commands/no-op.
    config.transitionMs=0;selectExpression(0,millis());run(20);
    responseTiming=ResponseTiming{};config.transitionMs=160;
    selectExpressionCommand(1,millis(),ResponseTiming::Uart,1,micros());
    selectExpressionCommand(2,millis(),ResponseTiming::Uart,1,micros());
    char ids[]="LATENCY";handleUsbCommand(ids,millis());
    assert(strstr(usbReply,"\"observation\":2")&&strstr(usbReply,"\"superseded_observation\":1"));
    selectExpressionCommand(2,millis(),ResponseTiming::Uart,1,micros());
    char skippedIds[]="LATENCY";handleUsbCommand(skippedIds,millis());
    assert(strstr(usbReply,"\"observation\":2")&&strstr(usbReply,"\"skipped_observation\":3"));
    responseTiming.accepted=responseTiming.completed=responseTiming.skipped=responseTiming.superseded=UINT32_MAX;
    responseTiming.expired=responseTiming.delayed=responseTiming.sequence=responseTiming.observation=UINT32_MAX;
    responseTiming.skippedSequence=responseTiming.supersededSequence=UINT32_MAX;
    responseTiming.skippedObservation=responseTiming.supersededObservation=UINT32_MAX;
    responseTiming.feedbackUs=responseTiming.onsetUs=responseTiming.completionUs=UINT32_MAX;
    responseTiming.maxFeedbackUs=responseTiming.maxOnsetUs=responseTiming.maxCompletionUs=UINT32_MAX;
    char maximal[]="LATENCY";handleUsbCommand(maximal,millis());
    assert(strstr(usbReply,"\"ok\":true")&&strstr(usbReply,"4294967295")&&strlen(usbReply)<sizeof(usbReply));
    std::cout<<"PASS: causal timing wrap, identities, stages, skipped/superseded/delayed/timeout, hidden/boop exclusion\n";
  } else if(mode=="entropy") {
    config.naturalBlinks=1;Entropy.ready=false;resetAnimations(millis());
    run(config.blinkInterval+config.blinkJitter+500);
    assert(!blinkRandomPending&&blinkFallbacks); // TRNG failure cannot stop natural blinks.
    const auto reads=Entropy.reads;
    const uint32_t before=blinkFallbacks;run(20000);assert(blinkFallbacks>before&&Entropy.reads==reads);
    Entropy.ready=true;resetAnimations(millis());serviceBlinkRandomness();assert(!blinkRandomPending);
    assert(Entropy.reads==reads+2);
    std::cout<<"PASS: deterministic bounded cosmetic fallback, repeated blink cycles, TRNG recovery\n";
  } else assert(false);
}
