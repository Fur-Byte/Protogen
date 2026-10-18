#pragma once
#include <stdint.h>
#include <limits.h>
// Diagnostic segment: validated UART command entry to completed visible SPI
// change/shutdown. It does not measure RF, target LED light or upstream clocks.
struct ResponseTiming {
  uint32_t accepted=0, completed=0, skipped=0, superseded=0, expired=0;
  uint32_t at=0, sequence=0, lastUs=0, maxUs=0;
  bool pending=false;
  static void add(uint32_t &n) {if(n!=UINT32_MAX)++n;}
  void start(uint32_t id,uint32_t now) {
    add(accepted);if(pending)add(superseded);sequence=id;at=now;pending=true;
  }
  void skip() {add(skipped);}
  void visible(uint32_t now) {
    if(!pending)return;
    lastUs=now-at;if(lastUs>maxUs)maxUs=lastUs;add(completed);pending=false;
  }
  void poll(uint32_t now) {if(pending&&now-at>=1000000){add(expired);pending=false;}}
};
