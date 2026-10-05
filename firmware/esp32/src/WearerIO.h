#pragma once
#include "DeviceConfig.h"
namespace WearerIO {
struct Status {
  float temperature=0,humidity=0,caseTemperature=0,volts=0,amps=0,pawBatteryVolts=0;
  double usedMWh=0;
  int remainingPercent=-1;
  bool environmentValid=false,energyValid=false,caseValid=false,estimateQualified=false;
  uint16_t faults=0;
};
void start(const Device::Config &config);
Status snapshot();
void face(const HelmetLink::Packet &status,bool radioConnected);
bool markFull();
}
