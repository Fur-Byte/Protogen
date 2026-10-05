#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
namespace SensorMath {
inline uint8_t shtCrc(const uint8_t *p,size_t n) {
  uint8_t crc=0xff;while(n--){crc^=*p++;for(unsigned b=0;b<8;++b)crc=uint8_t((crc<<1)^((crc&0x80)?0x31:0));}return crc;
}
inline bool sht40(const uint8_t b[6],float &t,float &h) {
  if(shtCrc(b,2)!=b[2] || shtCrc(b+3,2)!=b[5])return false;
  t=-45.0f+175.0f*(uint16_t(b[0])<<8|b[1])/65535.0f;
  h=-6.0f+125.0f*(uint16_t(b[3])<<8|b[4])/65535.0f;
  if(h<0)h=0;
  if(h>100)h=100;
  return isfinite(t) && t>=-10 && t<=70;
}
constexpr double CurrentLsb=5.0/524288.0;
constexpr double ShuntOhms=0.015; // Adafruit INA228 #5832's fitted 15 mOhm shunt.
constexpr uint16_t ShuntCalibration=1875;
constexpr double EnergyJoulesLsb=16.0*3.2*CurrentLsb;
inline uint64_t bigEndian(const uint8_t *p,size_t n) {uint64_t v=0;while(n--)v=(v<<8)|*p++;return v;}
inline int32_t signedCurrent(const uint8_t b[3]) {
  uint32_t v=static_cast<uint32_t>(bigEndian(b,3))>>4;
  return (v&0x80000)?static_cast<int32_t>(v)-0x100000:static_cast<int32_t>(v);
}
inline bool ntc(int mv,float &t) {
  // 15k to 3.3 V; 10k B3950 NTC to ground; calibrated millivolts.
  if(mv<100 || mv>2400)return false;
  const double resistance=15000.0*mv/(3300.0-mv);
  t=static_cast<float>(1.0/(1.0/298.15+log(resistance/10000.0)/3950.0)-273.15);
  return isfinite(t) && t>=0 && t<=65;
}
inline int percent(double used,uint32_t usable) {
  if(!usable || !isfinite(used) || used<0)return -1;
  double value=100.0*(1.0-used/usable);if(value<0)value=0;if(value>100)value=100;
  return static_cast<int>(value+0.5);
}
}
