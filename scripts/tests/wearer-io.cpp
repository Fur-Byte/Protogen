// Execute actual WearerIO task and display code with modeled hardware failures.
#include "WearerIO.cpp"
#include <algorithm>
#include <iostream>
using namespace WearerIO;
int main() {
  settings.features=Device::Screen|Device::BatteryCaseTemperature;
  settings.usableEnergyMWh=1000;
  FakeWearer::stopAfter=220;
  // Offset work completion by 3 ms: a 7 ms remaining deadline must yield,
  // never be rounded UP to a 10 ms tick. Also exercise temperature warnings.
  FakeWearer::beforeWait=[](){FakeWearer::us+=3000;};
  try {task(nullptr);} catch(const FakeWearer::Stop&) {}
  const auto status=snapshot();
  assert(status.caseValid && status.metrics.caseReads>0);
  assert(status.remainingPercent==-1); // Persisted energy never qualifies SOC.
  if(PROTOGEN_WEARER_DEADLINES) {
    assert(std::find(FakeWearer::notifications.begin(),FakeWearer::notifications.end(),0)!=FakeWearer::notifications.end());
  }
  HelmetLink::Packet p;p.payload[4]=0;
  face(p,true);
  Status panel;panel.remainingPercent=-1;
  assert(showScreen(panel,nowMs()));
  const unsigned before=FakeWearer::dataTransfers;
  assert(showScreen(panel,nowMs()));
  if(PROTOGEN_OLED_PAGES)assert(FakeWearer::dataTransfers==before);
  // Failed address/data writes must invalidate all pages, even after some
  // pages have succeeded. Recovery sends the complete image.
  if(PROTOGEN_OLED_PAGES) {
    panel.volts=15;
    FakeWearer::failTransfer=int(FakeWearer::transfers)+1;
    assert(!showScreen(panel,nowMs()));assert(validPages==0);
    FakeWearer::failTransfer=-1;
    const unsigned recovery=FakeWearer::dataTransfers;
    assert(showScreen(panel,nowMs()));assert(FakeWearer::dataTransfers==recovery+8);
  }
  if(PROTOGEN_OLED_PROTECTION) {
    p.payload[5]=HelmetLink::Sleeping;face(p,true);
    assert(showScreen(panel,nowMs()));assert(screenSleeping);
    p.payload[5]=0;face(p,true);
    const unsigned wake=FakeWearer::dataTransfers;
    assert(showScreen(panel,nowMs()));assert(!screenSleeping);
    assert(FakeWearer::dataTransfers==wake+(PROTOGEN_OLED_PAGES?8:1));
    p.payload[5]=HelmetLink::Sleeping;face(p,true);
    assert(showScreen(panel,nowMs()));assert(screenSleeping);
    panel.caseValid=true;panel.caseTemperature=51;
    assert(showScreen(panel,nowMs()));assert(!screenSleeping); // Warning wakes OLED.
    panel.caseValid=false;assert(showScreen(panel,nowMs()));assert(screenSleeping);
    FakeWearer::us+=2000000;
    assert(showScreen(panel,nowMs()));assert(!screenSleeping); // Stale link wakes OLED.
  }
  // Failure of a control command must force page recovery too. Exercise the
  // actual task failure handler (not merely showScreen's page branch).
  FakeWearer::us+=1000000;
  FakeWearer::waits=0;FakeWearer::stopAfter=4;
  FakeWearer::failTransfer=int(FakeWearer::transfers)+1;
  FakeWearer::beforeWait=[](){
    if(snapshot().faults&4) {
      assert(validPages==0);
      assert(!screenStateKnown);
      FakeWearer::failTransfer=-1;
    }
  };
  try {task(nullptr);} catch(const FakeWearer::Stop&) {}
  // Initialization may fail after the SPI handle exists. A subsequent frame
  // transfer must not clear the fault until reset/configuration succeeds.
  settings.features=Device::Screen;
  FakeWearer::us=0;FakeWearer::waits=0;FakeWearer::stopAfter=12;
  FakeWearer::transfers=0;FakeWearer::failTransfer=0;
  FakeWearer::commands.clear();
  const unsigned beforeRecovery=FakeWearer::dataTransfers;
  bool initializationFault=false, restored=false, jumped=false;
  FakeWearer::beforeWait=[&](){
    if(!FakeWearer::transfers)return;
    if(snapshot().faults&4) {
      initializationFault=true;
      assert(!screenConfigured);
      assert(FakeWearer::dataTransfers==beforeRecovery);
      FakeWearer::failTransfer=-1;
      if(!jumped){FakeWearer::us+=6000000;jumped=true;}
    } else if(initializationFault)restored=true;
  };
  try {task(nullptr);} catch(const FakeWearer::Stop&) {}
  assert(initializationFault && restored && screenConfigured);
  assert(!(snapshot().faults&4));
  assert(FakeWearer::dataTransfers>beforeRecovery);
  if(PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION) {
    uint8_t displayed[sizeof(frame)];memcpy(displayed,frame,sizeof(frame));
    Status expected=snapshot();assert(showScreen(expected,nowMs()));
    // The old "CHECK SENSORS" pixels must clear on the urgent pass, without
    // waiting the normal one-second interval after a successful recovery.
    assert(!memcmp(displayed,frame,sizeof(frame)));
  }
  std::cout<<"PASS: actual wearer deadlines, sensor warnings, changed pages, failed transfers, OLED sleep/wake/link restoration and unknown SOC\n";
}
