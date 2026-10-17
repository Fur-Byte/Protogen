#include "sketch.h"
static void run(uint32_t ms) { for(uint32_t i=0;i<ms;++i) { advanceTime(1000);loop(); } }
int main(int argc,char **argv) {
  assert(argc==2);setup();config.naturalBlinks=0;run(200);assert(microphoneSampling);
  const std::string test=argv[1];
  if(test=="isolation") {
#ifdef BoopSensor
    recordMicrophoneTimes=true;sensorAdc->adc1->stuck=true;run(20);
    assert(sensorFaults&2);sensorAdc->adc1->stuck=false;run(800);
    assert(!(sensorFaults&2) && microphoneSampling && sensorAdc->adc0->getTimerFrequency()==4000);
    assert(microphoneTimes.size()>3000);
    for(size_t i=1;i<microphoneTimes.size();++i)assert(microphoneTimes[i]-microphoneTimes[i-1]==250);
#else
    assert(false);
#endif
  } else if(test=="permanent") {
    pauseMicrophone();sensorAdc->adc0->validPin=false;run(2);
    assert(microphoneRecoveryState()==2 && microphoneLastFailure()==5);
    const unsigned starts=sensorAdc->adc0->starts;run(30000);assert(sensorAdc->adc0->starts==starts);
  } else if(test=="wrap") {
    pauseMicrophone();fakeUs=(uint64_t(0xffffffffu)-10)*1000;
    sensorAdc->adc0->rejectStart=true;run(2);assert(sensorFaults&4);
    sensorAdc->adc0->rejectStart=false;run(1200);
    assert(microphoneSampling && !(sensorFaults&4) && microphoneRecoveryCount()>0);
  } else if(test=="rail" || test=="invalid") {
    micInput=test=="rail" ? 1023 : -1;run(120);
    assert((sensorFaults&4) && !speakingActive);
    micInput=510;run(1200);assert(microphoneEnabled && microphoneSampling && !(sensorFaults&4));
    assert(sensorAdc->adc0->getTimerFrequency()==4000);
    assert(microphoneFailureCount()>0 && microphoneRetryCount()>0 && microphoneRecoveryCount()>0);
  } else if(test=="start") {
    pauseMicrophone();sensorAdc->adc0->rejectStart=true;run(2);
    assert((sensorFaults&4) && !microphoneSampling && !speakingActive);
    sensorAdc->adc0->rejectStart=false;run(1200);assert(microphoneSampling && !(sensorFaults&4));
  } else if(test=="stall") {
    sensorAdc->adc0->enabled=false;run(120);assert((sensorFaults&4) && !speakingActive);
    run(1200);assert(microphoneSampling && !(sensorFaults&4));
  } else if(test=="bounded") {
    micInput=1023;run(120);const unsigned before=sensorAdc->adc0->starts;
    run(30000);assert(sensorAdc->adc0->starts>before && sensorAdc->adc0->starts-before<20);
    micInput=510;run(6000);assert(microphoneSampling && !(sensorFaults&4));
  } else if(test=="disable") {
    micInput=1023;run(120);microphoneEnabled=false;const unsigned before=sensorAdc->adc0->starts;
    micInput=510;run(6000);assert(sensorAdc->adc0->starts==before && !microphoneSampling && !speakingActive);
    microphoneEnabled=true;run(1200);assert(microphoneSampling && !(sensorFaults&4));
  } else assert(false);
  pauseMicrophone();delete sensorAdc;
  std::cout<<"PASS: microphone recovery "<<test<<'\n';
}
