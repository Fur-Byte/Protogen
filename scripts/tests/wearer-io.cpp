// Execute actual WearerIO task and display code with modeled hardware failures.
#include "WearerIO.cpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <thread>
using namespace WearerIO;

static void expectRow(unsigned row, const char *label) {
  uint8_t *pixels = frame + row * 128;
  uint8_t actual[128];
  memcpy(actual, pixels, sizeof(actual));
  memset(pixels, 0, sizeof(actual));
  text(row, label);
  assert(!memcmp(actual, pixels, sizeof(actual)));
}

static void testConcurrentFaceUpdates() {
  Status panel;
  auto render = [&]() {
    assert(showScreen(panel, readFace()));
    std::array<uint8_t, sizeof(frame)> pixels;
    std::copy_n(frame, pixels.size(), pixels.begin());
    return pixels;
  };
  HelmetLink::Packet a{}, b{};
  b.payload[4] = 5;
  b.payload[5] = HelmetLink::Visible | HelmetLink::DisplayReady | HelmetLink::MicEnabled;
  HelmetLink::put16(b.payload + 10, 0x1234);
  face(a, false); const auto aOffline = render();
  face(b, true);  const auto bOnline = render();
  expectRow(2, "FACE WINK");
  expectRow(4, "PAW CONNECTED");
  expectRow(5, "MIC ON FAULT 4660");
  link(false);    const auto bOffline = render();
  assert(aOffline != bOffline && bOnline != bOffline);

  std::atomic<bool> running{false};
  std::thread uart([&]() {
    running.store(true);
    do { face(a, false); face(b, true); } while (running.load());
  });
  while (!running.load()) std::this_thread::yield();
  for (unsigned i = 0; i < 256; ++i) {
    link(false);
    const auto image = render();
    assert(image == aOffline || image == bOnline || image == bOffline);
  }
  running.store(false);
  uart.join();
}

static void testAdcInitializationRecovery() {
  settings.features=Device::BatteryCaseTemperature;
  adc=nullptr;calibration=nullptr;
  FakeWearer::adcCreates=FakeWearer::calibrations=0;
  FakeWearer::us=0;FakeWearer::stopAfter=0;
  FakeWearer::failCalibration=true;
  bool sawFault=false;
  FakeWearer::beforeWait=[&]() {
    const auto s=snapshot();
    assert(nowMs()<7000);
    if(s.faults&8){
      sawFault=true;
      assert(!s.caseValid);
      FakeWearer::failCalibration=false;
    } else if(sawFault && s.caseValid) {
      assert(nowMs()>=5000);
      throw FakeWearer::Stop{};
    }
  };
  try{task(nullptr);}catch(const FakeWearer::Stop&){}
  assert(sawFault && snapshot().caseValid && FakeWearer::adcCreates==1 && FakeWearer::calibrations==2);
}

int main() {
  settings.features=Device::Screen|Device::BatteryCaseTemperature;
  settings.usableEnergyMWh=1000;
  FakeWearer::stopAfter=220;
  // Offset work completion by3ms to exercise fractional/coarse tick deadlines
  // and the production1ms tick. Also exercise temperature warnings.
  FakeWearer::beforeWait=[](){FakeWearer::us+=3000;};
  try {task(nullptr);} catch(const FakeWearer::Stop&) {}
  const auto status=snapshot();
  assert(status.caseValid && status.metrics.caseReads>0);
  assert(status.remainingPercent==-1); // Persisted energy never qualifies SOC.
  if(PROTOGEN_WEARER_DEADLINES) {
    if(PROTOGEN_TEST_TICK_HZ==100) {
      assert(std::find(FakeWearer::notifications.begin(),FakeWearer::notifications.end(),0)!=FakeWearer::notifications.end());
    } else {
      assert(std::any_of(FakeWearer::notifications.begin(),FakeWearer::notifications.end(),[](TickType_t t){return t>0&&t%10!=0;}));
    }
  }
  Status panel;panel.remainingPercent=-1;
  HelmetLink::Packet p;
  assert(showScreen(panel,readFace()));
  expectRow(3,"FACE LINK LOST");
  const unsigned wake = PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION || PROTOGEN_WEARER_DEADLINES;
  const unsigned startWakes = FakeWearer::wakes;
  p.payload[5]=HelmetLink::Visible|HelmetLink::DisplayReady;
  face(p,true);
  assert(showScreen(panel,readFace()));expectRow(3,"FACE READY");
  ++p.sequence; ++p.payload[8]; face(p,true);
  assert(FakeWearer::wakes == startWakes + wake);
  p.epoch += 0x10000; face(p,true);
  assert(FakeWearer::wakes == startWakes + 2*wake);
  p.payload[5]=HelmetLink::Visible;
  face(p,true);assert(showScreen(panel,readFace()));expectRow(3,"FACE UNAVAILABLE");
  p.payload[5]=HelmetLink::DisplayReady;
  face(p,true);assert(showScreen(panel,readFace()));expectRow(3,"FACE HIDDEN");
  p.payload[5]=HelmetLink::Sleeping;
  face(p,true);assert(showScreen(panel,readFace()));expectRow(3,"FACE ASLEEP");
  assert(FakeWearer::wakes == startWakes + 5*wake);
  p.payload[5]=HelmetLink::Visible|HelmetLink::DisplayReady;face(p,true);
  FakeWearer::us+=500000;
  link(false);link(true);
  assert(readFace().validForMs==1500);
  assert(showScreen(panel,readFace()));
  const unsigned before=FakeWearer::dataTransfers;
  assert(showScreen(panel,readFace()));
  if(PROTOGEN_OLED_PAGES)assert(FakeWearer::dataTransfers==before);
  // Failed address/data writes must invalidate all pages, even after some
  // pages have succeeded. Recovery sends the complete image.
  if(PROTOGEN_OLED_PAGES) {
    panel.energyValid=true;panel.volts=15;
    FakeWearer::failTransfer=int(FakeWearer::transfers)+1;
    assert(!showScreen(panel,readFace()));assert(validPages==0);
    FakeWearer::failTransfer=-1;
    const unsigned recovery=FakeWearer::dataTransfers;
    assert(showScreen(panel,readFace()));assert(FakeWearer::dataTransfers==recovery+8);
  }
  if(PROTOGEN_OLED_PROTECTION) {
    p.payload[5]=HelmetLink::Sleeping;face(p,true);
    assert(showScreen(panel,readFace()));assert(screenSleeping);
    link(false);
    assert(showScreen(panel,readFace()));assert(!screenSleeping);
    link(true);
    assert(showScreen(panel,readFace()));assert(screenSleeping);
    p.payload[5]=0;face(p,true);
    const unsigned wake=FakeWearer::dataTransfers;
    assert(showScreen(panel,readFace()));assert(!screenSleeping);
    assert(FakeWearer::dataTransfers==wake+(PROTOGEN_OLED_PAGES?8:1));
    p.payload[5]=HelmetLink::Sleeping;face(p,true);
    assert(showScreen(panel,readFace()));assert(screenSleeping);
    panel.caseValid=true;panel.caseTemperature=51;
    assert(showScreen(panel,readFace()));assert(!screenSleeping); // Warning wakes OLED.
    panel.caseValid=false;assert(showScreen(panel,readFace()));assert(screenSleeping);
    FakeWearer::us+=2000000;
    assert(showScreen(panel,readFace()));assert(!screenSleeping); // Stale link wakes OLED.
    FakeWearer::us+=uint64_t(UINT32_MAX-1999)*1000;
    assert(showScreen(panel,readFace()));assert(!screenSleeping); // A long-absent face stays stale over millis wrap.
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
    Status expected=snapshot();assert(showScreen(expected,readFace()));
    // The old "CHECK SENSORS" pixels must clear on the urgent pass, without
    // waiting the normal one-second interval after a successful recovery.
    assert(!memcmp(displayed,frame,sizeof(frame)));
  }
  testAdcInitializationRecovery();
  FakeWearer::us = (uint64_t(UINT32_MAX) - 1000) * 1000;
  face(p,true);
  FakeWearer::us += 1999000;
  assert(readFace().validForMs == 1);
  FakeWearer::us += 1000;
  assert(readFace().validForMs == 0);
  const unsigned expiredWakes = FakeWearer::wakes;
  face(p,true);
  assert(FakeWearer::wakes == expiredWakes + wake && readFace().validForMs == 2000);
  testConcurrentFaceUpdates();
  std::cout<<"PASS: actual wearer deadlines, sensor warnings, changed pages, failed transfers, OLED sleep/wake/link restoration and unknown SOC\n";
}
