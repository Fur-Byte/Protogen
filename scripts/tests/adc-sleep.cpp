#include "sketch.h"

// Exercise production acquisition directly: changing display visibility must not
// consume an ADC result that was started in the previous visibility period.
static void serviceAfter(uint32_t ms) {
  advanceTime(uint64_t(ms) * 1000);
  serviceSensorAcquisition(millis());
}
static void beginBoop() {
  sleeping=false;visorVisible=true;connectorReady=true;
  SlowAdc::nextVoltage=false;
  SlowAdc::lastBoop=millis()-Settings::BoopPollMs;
  serviceSensorAcquisition(millis());
  assert(SlowAdc::pendingPin==irPin);
}
int main(int argc,char **argv) {
  assert(argc==2);
  setup();config.naturalBlinks=0;
  const std::string scenario=argv[1];
#ifdef Microphone
  startMicrophone();
  const auto starts=sensorAdc->adc0->starts;
  const auto stops=sensorAdc->adc0->stops;
  const auto resets=sensorAdc->adc0->resets;
  assert(sensorAdc->adc0->getTimerFrequency()==4000);
#endif
  if(scenario=="wrap")fakeUs=(uint64_t(UINT32_MAX)-4)*1000;
  for(unsigned i=0;i<200;++i) {
    beginBoop();
    const auto failures=sensorFailureCount();
    const auto boopAt=SlowAdc::boopAt;
    if(i&1)sleeping=true;else visorVisible=false;
    // The hardware may already have completed, or may still be converting.
    sensorAdc->adc1->stuck=(i&2)!=0;
    serviceSensorAcquisition(millis());
    if(scenario=="cancel")assert(SlowAdc::pendingPin!=irPin);
    sensorAdc->adc1->stuck=false;
    const unsigned hiddenMs=scenario=="rapid"?1:10000;
    for(unsigned ms=0;ms<hiddenMs;++ms)serviceAfter(1);
    sensorAdc->adc1->stuck=false;
    sleeping=false;visorVisible=true;connectorReady=true;
    // A VisorConnector rail remains serviced while hidden; finish its last
    // healthy conversion before requesting a new boop measurement.
    serviceSensorAcquisition(millis());
    assert(sensorFailureCount()==failures);
    assert(SlowAdc::boopAt==boopAt);
    assert(readBoopSensor()==-1);
    for(unsigned j=0;j<10 && readBoopSensor()<0;++j)serviceAfter(1);
    assert(readBoopSensor()==boopInput);
#ifdef VisorConnector
    assert(readDisplayVoltage()==voltageInput);
#endif
#ifdef Microphone
    assert(sensorAdc->adc0->starts==starts && sensorAdc->adc0->stops==stops && sensorAdc->adc0->resets==resets);
    assert(sensorAdc->adc0->getTimerFrequency()==4000);
#endif
  }
  // Real conversion faults still use bounded retry and recover after wake.
  beginBoop();sensorAdc->adc1->stuck=true;
  serviceAfter(Settings::SensorConversionTimeoutMs);
  assert(sensorLastFailure()==2 && sensorRecoveryState()==1);
  const auto retries=sensorRetryCount();
  serviceAfter(Settings::SensorRetryInitialMs-1);
  assert(sensorRetryCount()==retries);
  sensorAdc->adc1->stuck=false;
  for(unsigned i=0;i<20;++i)serviceAfter(1);
  assert(sensorRecoveryState()==0 && sensorRetryCount()==retries+1 && readBoopSensor()==boopInput);
  delete sensorAdc;
  std::cout<<"PASS: ADC1 sleep/hide "<<scenario<<" 200 cycles, freshness/retry/ADC0 isolation\n";
}
