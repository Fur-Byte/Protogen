#pragma once
#include "DeviceConfig.h"
namespace WearerIO {
struct Metrics {
  uint32_t executions=0,caseReads=0,environmentReads=0,energyReads=0;
  uint32_t oledPages=0,oledBytes=0,commitAttempts=0,commits=0,commitFailures=0,maxWorkUs=0;
  uint64_t workUs=0;
};
struct Status {
  Metrics metrics;
  float temperature=0,humidity=0,caseTemperature=0,volts=0,amps=0,pawBatteryVolts=0;
  double usedMWh=0;
  double measuredMWh=0; // INA deltas since boot, independent of FULL/SOC checkpoint.
  uint32_t measuredIntervals=0;
  float maximumCaseTemperature=0;
  int remainingPercent=-1;
  bool environmentValid=false,energyValid=false,caseValid=false,estimateQualified=false;
  uint16_t faults=0;
};
void start(const Device::Config &config);
Status snapshot();
void face(const HelmetLink::Packet &status,bool radioConnected);
bool markFull();
}
