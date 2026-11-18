#pragma once
#include "DeviceConfig.h"
#include "FlightRecorder.h"
namespace WearerIO {
enum class EnergyFault : uint8_t {
  None=0, Bus=1, Identity=2, NotReady=3, Trim=4, Overflow=5,
  Range=6, Counter=7, Stale=8, Configuration=9
};
struct Metrics {
  uint32_t executions=0,caseReads=0,environmentReads=0,energyReads=0;
  uint32_t oledPages=0,oledBytes=0,commitAttempts=0,commits=0,commitFailures=0,maxWorkUs=0;
  uint64_t workUs=0;
#if PROTOGEN_FLIGHT_RECORDER
  uint32_t p3StackMinimum=UINT32_MAX, p3WorkMaxUs=0;
#endif
};
struct Status {
  Metrics metrics;
  float temperature=0,humidity=0,caseTemperature=0,volts=0,amps=0,pawBatteryVolts=0;
  double usedMWh=0;
  double measuredMWh=0; // INA deltas since boot, independent of FULL/SOC checkpoint.
  uint32_t measuredIntervals=0;
  // Optional INA228 on the upstream PD rail. Not display-branch current.
  bool energyEnabled=false;
  EnergyFault energyFault=EnergyFault::None;
  uint32_t energySamples=0,energyValidAt=0,energyFailures=0,energyRecoveries=0;
  float maximumCaseTemperature=0;
  int remainingPercent=-1;
  bool environmentValid=false,energyValid=false,caseValid=false,estimateQualified=false;
  uint16_t faults=0;
};
void start(const Device::Config &config);
Status snapshot();
void face(const HelmetLink::Packet &status,bool radioConnected);
// A radio transition is not a fresh Teensy UART observation.
void link(bool radioConnected);
bool markFull();
}
