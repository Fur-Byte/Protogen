#include "sketch.h"
static void run(uint32_t ms) {
  for (uint32_t i=0;i<ms;++i) { advanceTime(1000); loop(); }
}
int main(int argc, char **argv) {
  assert(argc == 2);
  SPI.requireEnabledBuffer=true;
  setup(); config.naturalBlinks=0; run(200); assert(connectorReady);
  const std::string test=argv[1];
  if(test=="initialization") {
    assert(SPI.registers[11]==7 && SPI.registers[9]==0 && SPI.registers[15]==0);
    pilotInput=HIGH;run(2);SPI.registers.fill(0);config.brightness=15;
    pilotInput=LOW;run(100);assert(connectorReady && SPI.registers[11]==7);
    assert(SPI.maxIntensity<=Settings::MaxBrightness);
  } else if(test=="initdetach") {
    pilotInput=HIGH;run(2);pilotInput=LOW;
    SPI.beginHook=[](){pilotInput=HIGH;};run(100);
    assert(!connectorReady && digitalValues[4]==LOW && digitalValues[6]==LOW);
  } else if(test=="pilot") {
    assert(pinModes[12]==INPUT_PULLUP);
    pilotInput=HIGH;run(2);assert(!connectorReady && digitalValues[4]==LOW && digitalValues[6]==LOW);
    pilotInput=LOW;run(100);assert(connectorReady && pinModes[12]==INPUT_PULLUP);
    assert(matrix.begin(Settings::SpiFrequencyHz,true));assert(pinModes[12]==INPUT_PULLUP);
  } else if(test=="permanent") {
    sensorAdc->adc1->validPin=false;run(20);
    assert(sensorRecoveryState()==2 && sensorLastFailure()==5 && readDisplayVoltage()==-1);
    const unsigned starts=sensorAdc->adc1->starts;run(30000);assert(sensorAdc->adc1->starts==starts);
  } else if(test=="wrap") {
    fakeUs=(uint64_t(0xffffffffu)-10)*1000;run(1);
    sensorAdc->adc1->stuck=true;run(20);assert(sensorFaults&2);
    sensorAdc->adc1->stuck=false;run(800);assert(connectorReady && !(sensorFaults&2));
    assert(sensorFailureCount()>0 && sensorRetryCount()>0 && sensorRecoveryCount()>0);
  } else if(test=="timeout") {
    sensorAdc->adc1->stuck=true;run(20);
    assert((sensorFaults&2) && !connectorReady && readBoopSensor()==-1 && readDisplayVoltage()==-1);
    const unsigned starts=sensorAdc->adc1->starts;run(50);assert(sensorAdc->adc1->starts==starts);
    sensorAdc->adc1->stuck=false;run(800);
    assert(connectorReady && !(sensorFaults&2) && readBoopSensor()==1023);
    assert(sensorFailureCount()==1 && sensorRetryCount()==1 && sensorRecoveryCount()==1 && sensorLastFailure()==2);
  } else if(test=="invalid") {
    boopInput=-1;run(20);assert((sensorFaults&2) && readBoopSensor()==-1 && !boopArmed);
    boopInput=1023;run(800);assert(connectorReady && !(sensorFaults&2) && boopArmed);
  } else if(test=="start") {
    sensorAdc->adc1->rejectStart=true;run(20);assert((sensorFaults&2) && !connectorReady);
    sensorAdc->adc1->rejectStart=false;run(800);assert(connectorReady && !(sensorFaults&2));
  } else if(test=="boopstale") {
    boopState=boopArmed=true;advanceTime(30000);updateBoop(millis(),true);
    assert(!boopState && !boopArmed && boopReading==-1);
  } else if(test=="stalecompletion") {
    // A completed ADC result is stored in hardware; reading it after a stalled
    // loop must not relabel an old, safe voltage as a fresh rail measurement.
    while(SlowAdc::pendingPin!=Settings::DockVoltagePin)run(1);
    advanceTime(1000);voltageInput=0;advanceTime(100000);loop();
    assert(!connectorReady && digitalValues[4]==LOW && digitalValues[6]==LOW);
    assert(readDisplayVoltage()==-1 && sensorLastFailure()==2);
    voltageInput=640;run(800);assert(connectorReady && !(sensorFaults&2));
  } else if(test=="stale") {
    advanceTime(30000);updateVisorConnector(millis());
    assert(!connectorReady && digitalValues[4]==LOW && digitalValues[6]==LOW);
  } else if(test=="cycle") {
    pilotInput=HIGH;run(2);pilotInput=LOW;
    advanceTime(20000);updateVisorConnector(millis());assert(Dock::stage==1);
    advanceTime(50000);updateVisorConnector(millis());assert(!connectorReady && digitalValues[4]==LOW);
  } else if(test=="bounded") {
    sensorAdc->adc1->stuck=true;run(20);const unsigned starts=sensorAdc->adc1->starts;
    run(30000);assert(sensorAdc->adc1->starts>starts && sensorAdc->adc1->starts-starts<20);
    sensorAdc->adc1->stuck=false;run(6000);assert(connectorReady && !(sensorFaults&2));
  } else assert(false);
  delete sensorAdc;
  std::cout << "PASS: sensor/connector " << test << '\n';
}
