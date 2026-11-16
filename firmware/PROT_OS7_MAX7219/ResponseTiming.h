#pragma once
#include <stdint.h>
#include <limits.h>
#include "FlightInstrumentation.h"
// All clocks are Teensy-local. UART starts after validation, USB at dispatch,
// local at selection (after debounce). Physical/RF/button clocks are NOT inferred.
struct ResponseTiming {
  enum Origin : uint8_t { Local, Usb, Uart };
  enum Action : uint8_t { Expression, Visibility, Boop };
  enum Status : uint8_t { Idle, Pending, Complete, Timeout };
  uint32_t accepted=0, completed=0, skipped=0, superseded=0, expired=0, delayed=0;
  uint32_t at=0, sequence=0, observation=0, skippedSequence=0, supersededSequence=0;
  uint32_t skippedObservation=0, supersededObservation=0;
  uint32_t feedbackUs=0, onsetUs=0, completionUs=0;
  uint32_t maxFeedbackUs=0, maxOnsetUs=0, maxCompletionUs=0;
  Origin origin=Local, skippedOrigin=Local, supersededOrigin=Local;
  Action action=Expression;
  Status status=Idle;
  uint8_t source=0, target=0;
  bool pending=false, feedback=false, onset=false, wasDelayed=false, targetAlreadyVisible=false;
  static void add(uint32_t &n) {if(n!=UINT32_MAX)++n;}
  void start(uint32_t id,uint32_t now,Origin from,Action kind,uint8_t src,uint8_t dst) {
    add(accepted);
    if (pending) {
      add(superseded); supersededSequence=sequence; supersededOrigin=origin; supersededObservation=observation;
#if PROTOGEN_FLIGHT_RECORDER
      P3Teensy::emit(now, Flight::Superseded, action, observation);
#endif
    }
    sequence=id;observation=accepted;at=now;origin=from;action=kind;source=src;target=dst;
    pending=true;feedback=onset=wasDelayed=targetAlreadyVisible=false;status=Pending;
    feedbackUs=onsetUs=completionUs=0;
#if PROTOGEN_FLIGHT_RECORDER
    if (kind == Expression)
      P3Teensy::emit(now, Flight::Expression, dst | (unsigned(from) << 8), observation);
#endif
  }
  void skip(uint32_t id,Origin from) {add(accepted);add(skipped);skippedSequence=id;skippedOrigin=from;skippedObservation=accepted;}
  void delay() {
    if (pending && !wasDelayed) {
      add(delayed); wasDelayed = true;
#if PROTOGEN_FLIGHT_RECORDER
      P3Teensy::emit(P3Teensy::clockUs(), Flight::Delayed, action, observation);
#endif
    }
  }
  void observe(uint32_t now,bool causalFeedback,bool targetPixels,bool complete) {
    if(!pending)return;
    if(causalFeedback&&!feedback) {feedback=true;feedbackUs=now-at;if(feedbackUs>maxFeedbackUs)maxFeedbackUs=feedbackUs;}
    if(targetPixels&&!onset) {onset=true;onsetUs=now-at;if(onsetUs>maxOnsetUs)maxOnsetUs=onsetUs;}
    if(complete) {completionUs=now-at;if(completionUs>maxCompletionUs)maxCompletionUs=completionUs;
      add(completed);pending=false;status=Complete;
#if PROTOGEN_FLIGHT_RECORDER
      P3Teensy::emit(now, Flight::Completed, action, observation);
#endif
    }
  }
  void poll(uint32_t now) {
    // Long configured transitions and temporary safety blanking are legitimate.
    if (pending && now-at >= 5000000UL) {
      add(expired);pending=false;status=Timeout;
#if PROTOGEN_FLIGHT_RECORDER
      P3Teensy::emit(now, Flight::Expired, action, observation);
#endif
    }
  }
};
