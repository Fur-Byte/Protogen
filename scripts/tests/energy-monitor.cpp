// Existing optional INA228 only: actual register validation and wearer worker.
#include "WearerIO.cpp"
#include <iostream>
using namespace WearerIO;
static uint16_t flags=3;
static uint64_t counter=0;
static int failedRegister=-1;
static unsigned configurations=0;
static uint32_t busRaw=76800u<<4,currentRaw=52428u<<4;
static void model() {
  FakeWearer::i2cRead=[](void* device,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,int timeout) {
    assert(reinterpret_cast<uintptr_t>(device)==0x40 && tn==1 && timeout==10);
    const unsigned reg=tx[0];if(int(reg)==failedRegister)return -1;
    uint64_t value=0;
    if(reg==0x3e)value=0x5449;
    else if(reg==0x3f)value=0x2280;
    else if(reg==0x0b)value=flags;
    else if(reg==0x05)value=busRaw; // 15V input, not the 5V display branch.
    else if(reg==0x07)value=currentRaw; // Approximately 0.5A.
    else if(reg==0x09)value=counter;
    else assert(false);
    for(size_t i=rn;i>0;--i){rx[i-1]=value;value>>=8;}
    return ESP_OK;
  };
  FakeWearer::i2cWrite=[](void*,const uint8_t* data,size_t n,int timeout){
    assert(timeout==10 && n==3);
    if(data[0]==0x01)++configurations;
    return ESP_OK;
  };
}
// Exercise independent registration and transaction failures on one physical bus.
static void testSharedBusRecovery() {
  bus=nullptr;sht=nullptr;ina=nullptr;inaConfigured=false;
  failedRegister=-1;counter=0;
  FakeWearer::us=0;FakeWearer::busCreates=0;FakeWearer::failBus=true;
  FakeWearer::failI2cAddress=0x40;
  settings.features=Device::EnergyMonitor|Device::Environment;
  const auto inaWrite=FakeWearer::i2cWrite;
  bool failEnvironment=false;
  FakeWearer::i2cWrite=[&](void* device,const uint8_t* data,size_t n,int timeout){
    if(reinterpret_cast<uintptr_t>(device)==0x40)return inaWrite(device,data,n,timeout);
    assert(reinterpret_cast<uintptr_t>(device)==0x44 && n==1 && *data==0xfd && timeout==10);
    return ESP_OK;
  };
  FakeWearer::i2cReceive=[&](void* device,uint8_t* data,size_t n,int timeout){
    assert(reinterpret_cast<uintptr_t>(device)==0x44 && n==6 && timeout==10);
    if(failEnvironment)return -1;
    // Valid SHT40 temperature and humidity, approximately 25 C and 56% RH.
    data[0]=0x66;data[1]=0x66;data[2]=SensorMath::shtCrc(data,2);
    data[3]=0x80;data[4]=0;data[5]=SensorMath::shtCrc(data+3,2);
    return ESP_OK;
  };
  unsigned stage=0, baselineReads=0, baselineFailures=0;
  uint32_t stageAt=0;
  FakeWearer::beforeWait=[&](){
    const uint32_t now=nowMs();
    assert(now<40000);
    counter=FakeWearer::us/1000000*2048;
    const auto s=snapshot();
    if(!stage && (s.faults&3)==3) {
      FakeWearer::failBus=false;stage=1;
    } else if(stage==1 && s.environmentValid && s.energyFault==EnergyFault::Bus) {
      assert(FakeWearer::busCreates==2);
      stageAt=now;baselineReads=s.metrics.environmentReads;stage=2;
    } else if(stage==2 && now-stageAt>=5100) {
      assert(s.environmentValid && s.metrics.environmentReads>=baselineReads+5 && !s.energyValid);
      FakeWearer::failI2cAddress=-1;stage=3;
    } else if(stage==3 && s.energyValid) {
      assert(markFull());stage=4;
    } else if(stage==4 && s.estimateQualified) {
      baselineFailures=s.energyFailures;baselineReads=s.energySamples;
      failEnvironment=true;stageAt=now;stage=5;
    } else if(stage==5) {
      assert(s.energyValid && s.estimateQualified && s.energyFailures==baselineFailures);
      if(now-stageAt>=6000) {
        assert(!s.environmentValid && (s.faults&1) && s.energySamples>=baselineReads+5);
        failEnvironment=false;stage=6;
      }
    } else if(stage==6 && s.environmentValid) {
      assert(s.estimateQualified && !s.faults && FakeWearer::busCreates==2);
      stage=7;throw FakeWearer::Stop{};
    }
  };
  try{task(nullptr);}catch(const FakeWearer::Stop&){}
  assert(stage==7);
}
int main(int argc,char **argv) {
  model();
  const std::string mode=argc>1?argv[1]:"direct";
  if(mode=="direct") {
    ina=reinterpret_cast<void*>(0x40);assert(configureIna());
    float volts=0,amps=0;uint64_t count=0;
    assert(energy(volts,amps,count));assert(volts==15 && amps>0.49 && amps<0.51);
    for(const auto &fault:{std::pair<uint16_t,EnergyFault>{1,EnergyFault::NotReady},{0,EnergyFault::Trim},
        {0x0203,EnergyFault::Overflow},{0x0803,EnergyFault::Overflow}}) {
      flags=fault.first;assert(!energy(volts,amps,count) && inaFault==fault.second);
    }
    flags=3;failedRegister=7;busRaw=102400u<<4;
    assert(!energy(volts,amps,count) && inaFault==EnergyFault::Bus && volts==15);
    failedRegister=-1;busRaw=40960u<<4;
    assert(!energy(volts,amps,count) && inaFault==EnergyFault::Range && volts==15);
    busRaw=76800u<<4;currentRaw=524287u<<4;
    assert(!energy(volts,amps,count) && inaFault==EnergyFault::Range);
    std::cout<<"PASS: INA fresh conversion, trim/overflow/range/I2C rejection, atomic readings and 10ms transfer bound\n";
    return 0;
  }
  settings.features=mode=="disabled"?0:Device::EnergyMonitor;
  settings.usableEnergyMWh=1000;
  FakeWearer::stopAfter=0;
  unsigned stage=0;uint64_t bias=0;
  FakeWearer::beforeWait=[&](){
    if(FakeWearer::us>=22000000)throw FakeWearer::Stop{};
    counter=FakeWearer::us/1000000*2048+bias;
    const auto s=snapshot();
    if(mode=="disabled")return;
    assert(s.energyEnabled);
    if(mode=="bus") {
      if(!stage && s.energyValid){failedRegister=7;stage=1;}
      else if(stage==1 && s.energyFault==EnergyFault::Bus){assert(!s.energyValid);failedRegister=-1;stage=2;}
      else if(stage==2 && s.energyValid && s.energyRecoveries)stage=3;
      return;
    }
    if(!stage && s.energyValid) {assert(markFull());flags=1;stage=1;}
    else if(stage==1 && s.energyFailures) {
      assert(s.energyFault==EnergyFault::NotReady && !s.energyValid && !s.estimateQualified);
      // Model a FULL accepted from the previous valid published snapshot in
      // the clear-request / publish-invalid race. It belongs to the old fault
      // generation and must not qualify the post-fault reference.
      fullRequested.store(true);
      flags=3;stage=2;
    } else if(stage==2 && s.energyValid && s.energyRecoveries) {
      assert(!s.estimateQualified); // The request made before the fault was discarded.
      assert(markFull());bias=1000000000;counter+=bias;stage=3;
    } else if(stage==3 && s.estimateQualified){assert(false && "FULL must not qualify an implausible energy jump");}
    else if(stage==3 && s.energyFault==EnergyFault::Counter) {
      assert(!s.energyValid && !s.estimateQualified && s.remainingPercent==-1);
      fullRequested.store(true); // Same stale-publication race after a counter fault.
      stage=4;
    } else if(stage==4 && s.energyValid) {
      assert(!s.estimateQualified && s.energyRecoveries>=2);
      assert(markFull());stage=5;
    } else if(stage==5 && s.estimateQualified)stage=6;
  };
  try{task(nullptr);}catch(const FakeWearer::Stop&){}
  const auto s=snapshot();
  if(mode=="disabled")assert(!s.energyEnabled && !s.energyValid && !s.metrics.energyReads && !configurations);
  else {
    assert(stage==(mode=="bus"?3u:6u));
    assert(s.energyValid && s.energySamples>1 && s.energyValidAt>0 && s.energyFault==EnergyFault::None);
    assert(s.energyFailures>0 && s.energyRecoveries>0 && s.metrics.energyReads<25 && configurations<5);
    assert(s.volts==15 && s.amps>0.49 && s.amps<0.51);
  }
  if(mode=="bus")testSharedBusRecovery();
  std::cout<<"PASS: optional INA task "<<mode<<", bounded polling/reconfiguration, fault/recovery diagnostics and valid-only input samples\n";
}
