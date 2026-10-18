#include "sketch.h"

static void checkOriginalGuards() {
  ControllerIdle::SafetyState state;
  assert(ControllerIdle::guard(state)==ControllerIdle::Ready);
  state.primask=1;assert(ControllerIdle::guard(state)==ControllerIdle::Context);state.primask=0;
  state.exception=15;assert(ControllerIdle::guard(state)==ControllerIdle::Context);state.exception=0;
  state.sleepControl=4;assert(ControllerIdle::guard(state)==ControllerIdle::SleepMode);state.sleepControl=0;
  state.clockControl=1;assert(ControllerIdle::guard(state)==ControllerIdle::SleepMode);state.clockControl=0;
  state.tickControl=1;assert(ControllerIdle::guard(state)==ControllerIdle::Tick);
  state.tickControl=3|(1u<<16);assert(ControllerIdle::guard(state)==ControllerIdle::Ready); // COUNTFLAG is not configuration.
  for(uint32_t mode=1;mode<4;++mode){state.clockControl=mode;assert(ControllerIdle::guard(state)==ControllerIdle::SleepMode);}
}
static void checkAdditionalGuard(const std::string &name) {
  ControllerIdle::SafetyState state;
  if(name=="basepri")state.basepri=32;
  else if(name=="faultmask")state.faultmask=1;
  else if(name=="sleeponexit")state.sleepControl=2;
  else if(name=="ticksource")state.tickControl=7;
  else if(name=="tickslow")state.tickReload=100;
  else if(name=="tickfast")state.tickReload=0;
  else assert(false);
  const auto expected=name=="basepri" || name=="faultmask" ? ControllerIdle::Context :
    name=="sleeponexit" ? ControllerIdle::SleepMode : ControllerIdle::Tick;
  assert(ControllerIdle::guard(state)==expected);
}
static void checkCounters() {
  ControllerIdle idle;
  ControllerIdle::SafetyState state;state.basepri=32;
  assert(!idle.permit(state));
  assert(idle.attempts==1 && idle.declined==1 && idle.lastGuard==ControllerIdle::Context && idle.waits==0);
  state.basepri=0;assert(idle.permit(state));
  assert(idle.attempts==2 && idle.declined==1 && idle.lastGuard==ControllerIdle::Ready);
  idle.recordWait(0);idle.recordWait(2000);idle.recordWait(2001);
  assert(idle.waits==3 && idle.totalWaitUs==4001 && idle.maxWaitUs==2001 && idle.overBudgetWaits==1);
  idle.attempts=idle.declined=idle.waits=idle.overBudgetWaits=UINT32_MAX;
  idle.totalWaitUs=UINT64_MAX-5;state.faultmask=1;
  assert(!idle.permit(state));idle.recordWait(2001);
  assert(idle.attempts==UINT32_MAX && idle.declined==UINT32_MAX && idle.waits==UINT32_MAX &&
    idle.overBudgetWaits==UINT32_MAX && idle.totalWaitUs==UINT64_MAX);
}
static void checkBackpressure(bool uart,bool queuedRx) {
  auto &port=uart ? Serial1 : Serial;
  auto &sent=uart ? PawLink::txSent : usbReplySent;
  auto &length=uart ? PawLink::txLength : usbReplyLength;
  if(uart){length=sizeof(PawLink::tx);memset(PawLink::tx,0xa5,length);}
  else {length=384;memset(usbReply,'x',length);usbReply[length]=0;}
  const auto expected=length;sent=0;port.capacity=0;port.output.clear();
  if(queuedRx)port.input=std::string(130,'x');
  const auto waits=controllerIdle.waits;loop();
  assert(controllerIdle.waits==waits+1 && sent==0);
  assert(!queuedRx || port.available()==130);
  // Runnable partial transmission must drain without waiting for another tick.
  port.capacity=16;const auto blockedWaits=controllerIdle.waits;loop();
  assert(sent==16 && controllerIdle.waits==blockedWaits);
  port.input.clear();port.capacity=4096;
  while(sent<length)loop();
  assert(port.output.size()==expected);length=sent=0;
}
int main(int argc,char **argv) {
  checkOriginalGuards();
  if(argc==2){
    const std::string scenario=argv[1];
    if(scenario=="backpressureUSB" || scenario=="backpressureUART") {
      setup();loop();sleeping=true;loop();checkBackpressure(scenario=="backpressureUART",true);
    } else if(scenario=="counters")checkCounters();else checkAdditionalGuard(scenario);
    return 0;
  }
  checkCounters();
  for(const auto *name:{"basepri","faultmask","sleeponexit","ticksource","tickslow","tickfast"})checkAdditionalGuard(name);
  setup(); loop();
  char diagnostic[] = "SENSORS"; handleUsbCommand(diagnostic,millis());
  assert(strstr(usbReply,"\"adc\":{\"state\":0"));
  assert(strstr(usbReply,"\"microphone\":{\"state\":0"));
  const auto visibleWaits = controllerIdle.waits;
  for (unsigned i = 0; i < 100; ++i) loop();
  assert(controllerIdle.waits == visibleWaits);
  sleeping = true; loop();
  assert(controllerIdle.waits == visibleWaits + 1);
  assert(displayBlanked && SPI.shutdown);
  // A pending reply or buffered input is work: never park with a serial backlog.
  const auto hiddenWaits = controllerIdle.waits;
  Serial.input = std::string(130, 'x');
  loop(); assert(controllerIdle.waits == hiddenWaits && Serial.available() > 0);
  Serial.input.clear(); usbLineLength = 0; usbOverflow = false;
  Serial.input = "STATUS\n";
  loop(); assert(controllerIdle.waits == hiddenWaits);
  while (usbReplySent < usbReplyLength) loop();
  assert(Serial.output.find("\"ok\":true") != std::string::npos);
  Serial1.input = std::string(130, 'x');
  const auto uartWaits = controllerIdle.waits;
  loop(); assert(controllerIdle.waits == uartWaits && Serial1.available() > 0);
  Serial1.input.clear();
  checkBackpressure(false,false);checkBackpressure(true,false);
  checkBackpressure(false,true);checkBackpressure(true,true);
  // Disconnected USB must not strand a reply or prevent interrupt waiting.
  Serial.connected=false;usbReplyLength=10;usbReplySent=0;
  const auto disconnectedWaits=controllerIdle.waits;loop();
  assert(controllerIdle.waits==disconnectedWaits+1 && usbReplyLength==0);
  Serial.connected=true;
  // Poll-based button wake remains in the ordinary loop after every interrupt.
  buttonInput = LOW;
  advanceTime(1000); loop();
  advanceTime((Settings::ButtonDebounceMs + 1) * 1000); loop();
  buttonInput = HIGH;
  advanceTime(1000); loop();
  advanceTime((Settings::ButtonDebounceMs + 1) * 1000); loop();
  assert(!sleeping && !displayBlanked && !SPI.shutdown);
  // The same policy applies to Hall-hidden and display/dock-disconnected paths.
  const auto awakeWaits=controllerIdle.waits;visorVisible=false;loop();
  assert(controllerIdle.waits==awakeWaits+1 && displayBlanked && SPI.shutdown);
  visorVisible=true;loop();assert(!displayBlanked && !SPI.shutdown);
  displayReady=false;const auto absentDisplayWaits=controllerIdle.waits;loop();
  assert(controllerIdle.waits==absentDisplayWaits+1 && SPI.shutdown);
  displayReady=true;connectorReady=false;const auto absentDockWaits=controllerIdle.waits;loop();
  assert(controllerIdle.waits==absentDockWaits+1 && SPI.shutdown);
  connectorReady=true;loop();assert(!displayBlanked && !SPI.shutdown);
  const auto wakes = renderMetrics.wakes;
  advanceTime(1000); loop(); assert(renderMetrics.wakes == wakes);
  char idleDiagnostic[]="IDLE";handleUsbCommand(idleDiagnostic,millis());
  assert(strstr(usbReply,"\"ok\":true") && strstr(usbReply,"\"attempts\":") &&
    strstr(usbReply,"\"declined\":") && strstr(usbReply,"\"last_guard\":") &&
    strstr(usbReply,"\"over_budget_waits\":"));
  std::cout << "PASS: shallow idle guards/counter saturation, USB/UART runnable/backpressured queues, hidden/disconnected modes and button wake (host; no WFI/current measurement)\n";
}
