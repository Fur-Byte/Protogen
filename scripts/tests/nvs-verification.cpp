#include "SecureStorage.h"
#include <cassert>
#include <iostream>
#include <vector>
namespace { std::vector<uint8_t> staged, persisted; unsigned failure=0; }
namespace Wireless { namespace Crypto {
void wipe(void *p,size_t n){protogenSecureZero(p,n);}
}}
int nvs_flash_init_partition(const char *){return ESP_OK;}
int nvs_open(const char *,int,nvs_handle_t *h){*h=1;return ESP_OK;}
int nvs_set_blob(nvs_handle_t,const char *,const void *p,size_t n){
  if(failure==1)return -1;
  const auto *bytes=static_cast<const uint8_t *>(p);staged.assign(bytes,bytes+n);return ESP_OK;
}
int nvs_commit(nvs_handle_t){if(failure==2)return -1;persisted=staged;return ESP_OK;}
int nvs_get_blob(nvs_handle_t,const char *,void *p,size_t *n){
  if(failure==3 || *n<persisted.size())return -1;
  memcpy(p,persisted.data(),persisted.size());*n=persisted.size();
  if(failure==4)static_cast<uint8_t *>(p)[5]^=1;
  if(failure==5)--*n;
  return ESP_OK;
}
int main(){
  Device::Config config;config.features=3;config.usableEnergyMWh=1000;
  nvs_handle_t handle=0;assert(SecureStorage::begin(handle));
  assert(SecureStorage::save(handle,config));
  Device::Config loaded;assert(SecureStorage::load(handle,loaded));
  assert(loaded.features==3 && loaded.usableEnergyMWh==1000);
  const auto previous=persisted;
  for(failure=1;failure<=5;++failure){
    config.usableEnergyMWh=1000+failure;
    assert(!SecureStorage::save(handle,config));
    if(failure<=2)assert(persisted==previous);
  }
  failure=0;assert(SecureStorage::save(handle,config));
  const auto m=SecureStorage::metrics();
  assert(m.attempts==7 && m.commits==5 && m.verified==2 && m.failures==5);
  std::cout<<"PASS: actual NVS save path, set/commit/read failures, corrupted/short readback and verified write counters\n";
}
