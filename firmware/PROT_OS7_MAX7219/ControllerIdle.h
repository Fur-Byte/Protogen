#pragma once
#include <stdint.h>
#include <limits.h>

// Cortex-M shallow WFI preserves clocks, SysTick, UART, USB and ADC interrupts.
// The pinned Teensy core enables a 1 ms SysTick: polled inputs remain serviced
// at that cadence even when no external interrupt occurs. Never use deep sleep.
struct ControllerIdle {
  uint32_t waits = 0, maxWaitUs = 0;
  uint64_t totalWaitUs = 0;
  void wait() {
    const uint32_t started = micros();
#if defined(__IMXRT1062__)
    uint32_t masked, exception;
    asm volatile("mrs %0, primask" : "=r"(masked));
    asm volatile("mrs %0, ipsr" : "=r"(exception));
    if (masked || exception || (SCB_SCR & SCB_SCR_SLEEPDEEP) ||
        (CCM_CLPCR & CCM_CLPCR_LPM(3)) ||
        (SYST_CSR & (SYST_CSR_TICKINT | SYST_CSR_ENABLE)) !=
          (SYST_CSR_TICKINT | SYST_CSR_ENABLE)) return;
    asm volatile("dsb\n\twfi\n\tisb" ::: "memory");
#endif
    if (waits != UINT32_MAX) ++waits;
    const uint32_t elapsed = micros() - started;
    totalWaitUs += elapsed;
    if (elapsed > maxWaitUs) maxWaitUs = elapsed;
  }
};
