#include "UartMac.h"
UartSecurity::MbedMac productionMac;
bool productionTeensyMac(const uint8_t *p,size_t n,uint8_t out[32]) {return productionMac.authenticate(p,n,out);}
