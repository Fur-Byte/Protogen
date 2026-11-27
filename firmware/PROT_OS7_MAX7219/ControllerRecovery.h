#pragma once
#include "RecoveryJournal.h"

#ifndef PROTOGEN_CONTROLLER_RECOVERY
#define PROTOGEN_CONTROLLER_RECOVERY 0
#endif
static_assert(PROTOGEN_CONTROLLER_RECOVERY == 0 || PROTOGEN_CONTROLLER_RECOVERY == 1,
              "Controller recovery must be 0 or 1");

#if PROTOGEN_CONTROLLER_RECOVERY
#if !defined(__IMXRT1062__) || !defined(PROTOGEN_RECOVERY_BUILD_ID)
#error "Recovery requires RT1062 and an independently assigned unique build identity"
#endif
#include <Arduino.h>
namespace Recovery {
constexpr uint64_t BuildId = PROTOGEN_RECOVERY_BUILD_ID;
static_assert(BuildId != 0, "Recovery build identity must be nonzero");
extern Slot retained[2];

class Controller {
  int active = -1;
  uint32_t sequence = 0, lastFeed = 0;
  HealthyWindow health;

  void flush(unsigned index) {
    arm_dcache_flush_delete(&retained[index], sizeof(Slot));
    asm volatile("dsb" ::: "memory");
  }
  bool publish(Stage stage, uint32_t now, uint32_t renders, uint32_t drained) {
    const unsigned next = active == 0 ? 1 : 0;
    Slot &target = retained[next];
    const Slot value = make(BuildId, sequence + 1u, attempts, stage, now, passes, renders, drained, cause);
    // Invalidate the inactive slot before writing its body. Flush at each
    // boundary so a reset cannot publish a partially written replacement.
    target.marker = 0;
    flush(next);
    Slot body = value;
    body.marker = 0;
    target = body;
    flush(next);
    target.marker = Commit;
    flush(next);
    if (!valid(target, BuildId)) return false;
    active = next;
    sequence = value.sequence;
    return true;
  }
  void feed() {
    WDOG1_WSR = 0x5555;
    WDOG1_WSR = 0xaaaa;
  }
  bool arm() {
    if (WDOG1_WCR & WDOG_WCR_WDE) return false;
    WDOG1_WMCR = 0;
    WDOG1_WICR = 0;
    // Internal functional reset, 2 s candidate deadline, no external reset.
    const uint16_t control = WDOG_WCR_WT(3) | WDOG_WCR_SRS | WDOG_WCR_WDA;
    WDOG1_WCR = control;
    WDOG1_WCR = control | WDOG_WCR_WDE;
    feed();
    return (WDOG1_WCR & WDOG_WCR_WDE) != 0;
  }

public:
  uint32_t cause = 0, attempts = RetryLimit, passes = 0;
  bool armed = false, locked = true, previousValid = false;
  Slot previous{};

  void begin() {
    cause = SRC_SRSR;
    SRC_SRSR = cause;
    CCM_CCGR3 |= CCM_CCGR3_WDOG1(3);
    active = newest(retained[0], retained[1], BuildId);
    const bool trusted = active >= 0 && (cause == Watchdog || cause == Software);
    if (trusted) {
      previous = retained[active];
      previousValid = true;
      sequence = previous.sequence;
    } else {
      // Reject old identity, ambiguous reset or cold start before publication.
      for (unsigned i = 0; i < 2; ++i) {
        retained[i].marker = 0;
        flush(i);
      }
      active = -1;
    }
    attempts = bootAttempts(cause, trusted, previous.attempts);
    locked = attempts >= RetryLimit || (WDOG1_WCR & WDOG_WCR_WDE);
    if (!publish(Boot, millis(), 0, 0)) locked = true;
  }

  // Only the foreground owner calls this after a complete useful pass.
  // Interrupt activity, USB traffic and repeated calls inside saves do not feed.
  void completed(uint32_t now, bool progressing, uint32_t renders, uint32_t drained) {
    if (locked) return;
    if (passes != UINT32_MAX) ++passes;
    const bool recovered = health.observe(now, progressing);
    if (!progressing || (armed && uint32_t(now - lastFeed) < FeedMs)) return;
    if (recovered) attempts = 0;
    if (!publish(Running, now, renders, drained)) {
      locked = true;
      return;
    }
    if (!armed) {
      armed = arm();
      if (!armed) locked = true;
    } else {
      feed();
    }
    lastFeed = now;
  }

  void saving(uint32_t now, uint32_t renders, uint32_t drained) {
    if (armed && !locked && !publish(Saving, now, renders, drained)) locked = true;
    // Never extend the hardware deadline from a save.
  }
};
}
#endif
