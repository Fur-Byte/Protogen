#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>
#include <cassert>
using TickType_t=uint32_t;
using TaskHandle_t=void*;
using portMUX_TYPE=int;
constexpr int portMUX_INITIALIZER_UNLOCKED=0, pdTRUE=1, pdPASS=1, ESP_OK=0;
#ifndef PROTOGEN_TEST_TICK_HZ
#define PROTOGEN_TEST_TICK_HZ 1000
#endif
#define pdMS_TO_TICKS(ms) ((ms)*PROTOGEN_TEST_TICK_HZ/1000)
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
namespace FakeWearer {
inline uint64_t us=0;
inline unsigned waits=0, stopAfter=0, adcReads=0, commits=0;
inline bool commitFail=false;
inline uint32_t stored=0, staged=0;
inline int mv=1320;
inline std::vector<TickType_t> notifications;
inline std::function<void()> beforeWait;
inline std::function<int(void*,const uint8_t*,size_t,uint8_t*,size_t,int)> i2cRead;
inline std::function<int(void*,const uint8_t*,size_t,int)> i2cWrite;
inline int failTransfer=-1;
inline unsigned transfers=0, dataTransfers=0;
inline bool dc=false;
inline std::vector<std::vector<uint8_t>> commands;
struct Stop {};
inline void wait(TickType_t ticks) {
  if(beforeWait)beforeWait();
  if(stopAfter && ++waits>=stopAfter)throw Stop{};
  // A yielding poll advances modeled time without sleeping a full tick.
  us+=ticks?uint64_t(ticks)*1000000/PROTOGEN_TEST_TICK_HZ:1000;
}
}
inline int64_t esp_timer_get_time(){return FakeWearer::us;}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
inline void xTaskNotifyGive(TaskHandle_t){}
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*){return pdPASS;}
inline uint32_t ulTaskNotifyTake(int,TickType_t ticks){FakeWearer::notifications.push_back(ticks);if(ticks)FakeWearer::wait(ticks);return 0;}
inline void vTaskDelay(TickType_t ticks){FakeWearer::wait(ticks);}
using adc_oneshot_unit_handle_t=void*;
using adc_cali_handle_t=void*;
constexpr int ADC_UNIT_1=1, ADC_BITWIDTH_DEFAULT=12, ADC_ATTEN_DB_12=12, ADC_CHANNEL_3=3;
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
struct adc_oneshot_unit_init_cfg_t{int unit_id=0;};
struct adc_oneshot_chan_cfg_t{int bitwidth=0,atten=0;};
struct adc_cali_curve_fitting_config_t{int unit_id=0,chan=0,atten=0,bitwidth=0;};
inline int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*,void** p){*p=reinterpret_cast<void*>(1);return ESP_OK;}
inline int adc_oneshot_config_channel(void*,int,const adc_oneshot_chan_cfg_t*){return ESP_OK;}
inline int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*,void** p){*p=reinterpret_cast<void*>(1);return ESP_OK;}
inline int adc_oneshot_read(void*,int,int* raw){++FakeWearer::adcReads;*raw=FakeWearer::mv;return ESP_OK;}
inline int adc_cali_raw_to_voltage(void*,int raw,int* mv){*mv=raw;return ESP_OK;}
using gpio_num_t=int;
constexpr int GPIO_MODE_OUTPUT=1;
inline int gpio_set_direction(int,int){return ESP_OK;}
inline int gpio_set_level(int pin,int value){if(pin==5)FakeWearer::dc=value;return ESP_OK;}
using i2c_master_bus_handle_t=void*;
using i2c_master_dev_handle_t=void*;
constexpr int I2C_ADDR_BIT_LEN_7=7,I2C_NUM_0=0,I2C_CLK_SRC_DEFAULT=0;
struct i2c_device_config_t{int dev_addr_length=0,device_address=0,scl_speed_hz=0;};
struct i2c_master_bus_config_t{int i2c_port=0,sda_io_num=0,scl_io_num=0,clk_source=0,glitch_ignore_cnt=0;};
inline int i2c_new_master_bus(const i2c_master_bus_config_t*,void** p){*p=reinterpret_cast<void*>(1);return ESP_OK;}
inline int i2c_master_bus_add_device(void*,const i2c_device_config_t* c,void** p){*p=reinterpret_cast<void*>(uintptr_t(c->device_address));return ESP_OK;}
inline int i2c_master_transmit(void* d,const uint8_t* b,size_t n,int timeout){return FakeWearer::i2cWrite?FakeWearer::i2cWrite(d,b,n,timeout):ESP_OK;}
inline int i2c_master_receive(void*,uint8_t*,size_t,int){return -1;}
inline int i2c_master_transmit_receive(void* d,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,int timeout){return FakeWearer::i2cRead?FakeWearer::i2cRead(d,tx,tn,rx,rn,timeout):-1;}
using spi_device_handle_t=void*;
constexpr int SPI2_HOST=2,SPI_DMA_CH_AUTO=0;
struct spi_transaction_t{size_t length=0;const void* tx_buffer=nullptr;};
struct spi_bus_config_t{int mosi_io_num=0,miso_io_num=0,sclk_io_num=0,quadwp_io_num=0,quadhd_io_num=0,max_transfer_sz=0;};
struct spi_device_interface_config_t{int clock_speed_hz=0,mode=0,spics_io_num=0,queue_size=0;};
inline int spi_bus_initialize(int,const spi_bus_config_t*,int){return ESP_OK;}
inline int spi_bus_add_device(int,const spi_device_interface_config_t*,void** p){*p=reinterpret_cast<void*>(1);return ESP_OK;}
inline int spi_device_polling_transmit(void*,const spi_transaction_t* t){
  const unsigned index=FakeWearer::transfers++;
  if(FakeWearer::dc)++FakeWearer::dataTransfers;
  else {const auto* b=static_cast<const uint8_t*>(t->tx_buffer);FakeWearer::commands.emplace_back(b,b+t->length/8);}
  return int(index)==FakeWearer::failTransfer?-1:ESP_OK;
}
using nvs_handle_t=unsigned;
constexpr int NVS_READWRITE=1;
inline int nvs_open(const char*,int,unsigned* h){*h=1;return ESP_OK;}
inline int nvs_get_u32(unsigned,const char*,uint32_t* value){*value=FakeWearer::stored;return ESP_OK;}
inline int nvs_set_u32(unsigned,const char*,uint32_t value){FakeWearer::staged=value;return ESP_OK;}
inline int nvs_commit(unsigned){++FakeWearer::commits;if(FakeWearer::commitFail)return -1;FakeWearer::stored=FakeWearer::staged;return ESP_OK;}
