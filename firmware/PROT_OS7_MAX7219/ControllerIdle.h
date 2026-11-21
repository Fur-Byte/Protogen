#pragma once
#include <stdint.h>
#include <limits.h>

// Shallow WFI only: no clock/register writes and no deep sleep. The pinned
// Teensy core's 100 kHz external SysTick/reload 99 provides the periodic wake.
struct ControllerIdle {
  static constexpr uint32_t SleepDeep = 4, SleepOnExit = 2, LowPowerMask = 3;
  static constexpr uint32_t TickConfigMask = 7, TickEnabled = 3, TickReload = 99;
  struct SafetyState {
    uint32_t primask = 0, exception = 0, basepri = 0, faultmask = 0;
    uint32_t sleepControl = 0, clockControl = 0, tickControl = TickEnabled, tickReload = TickReload;
  };
  enum Guard : uint8_t { Ready = 0, Context = 1, SleepMode = 2, Tick = 3 };
  // Test the same policy used at WFI entry, including unexpected library/core
  // configuration changes. Fall back to polling rather than alter caller state.
  static constexpr Guard guard(const SafetyState &state) {
    return state.primask || state.exception || state.basepri || state.faultmask ? Context
           : (state.sleepControl & (SleepDeep | SleepOnExit)) || (state.clockControl & LowPowerMask)
               ? SleepMode
           : (state.tickControl & TickConfigMask) != TickEnabled || state.tickReload != TickReload ? Tick
                                                                                                   : Ready;
  }
  static constexpr uint32_t WaitBudgetUs = 2000; // Diagnostic threshold, includes ISR time.
  uint32_t waits = 0, maxWaitUs = 0, attempts = 0, declined = 0, overBudgetWaits = 0;
  uint64_t totalWaitUs = 0;
  Guard lastGuard = Ready;
  bool permit(const SafetyState &state) {
    if (attempts != UINT32_MAX) ++attempts;
    lastGuard = guard(state);
    if (lastGuard != Ready) {
      if (declined != UINT32_MAX) ++declined;
      return false;
    }
    return true;
  }
  void recordWait(uint32_t elapsed) {
    if (waits != UINT32_MAX) ++waits;
    totalWaitUs = UINT64_MAX - totalWaitUs < elapsed ? UINT64_MAX : totalWaitUs + elapsed;
    if (elapsed > WaitBudgetUs && overBudgetWaits != UINT32_MAX) ++overBudgetWaits;
    if (elapsed > maxWaitUs) maxWaitUs = elapsed;
  }
  void wait() {
    SafetyState state;
#if defined(__IMXRT1062__)
    static_assert(SleepDeep == SCB_SCR_SLEEPDEEP && SleepOnExit == SCB_SCR_SLEEPONEXIT &&
                      LowPowerMask == CCM_CLPCR_LPM(3) &&
                      TickEnabled == (SYST_CSR_TICKINT | SYST_CSR_ENABLE) &&
                      TickConfigMask == (SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE),
                  "Idle guard masks must match the pinned Teensy core.");
    asm volatile("mrs %0, primask" : "=r"(state.primask));
    asm volatile("mrs %0, ipsr" : "=r"(state.exception));
    asm volatile("mrs %0, basepri" : "=r"(state.basepri));
    asm volatile("mrs %0, faultmask" : "=r"(state.faultmask));
    state.sleepControl = SCB_SCR;
    state.clockControl = CCM_CLPCR;
    state.tickControl = SYST_CSR;
    state.tickReload = SYST_RVR;
#endif
    if (!permit(state)) return;
    const uint32_t started = micros();
#if defined(__IMXRT1062__)
    asm volatile("dsb\n\twfi\n\tisb" ::: "memory");
#endif
    recordWait(micros() - started);
  }
};
