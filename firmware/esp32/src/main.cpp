#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "SecureSession.h"
#include "WearerIO.h"

namespace {
uint32_t nowMs(){return static_cast<uint32_t>(esp_timer_get_time()/1000);}
uint32_t clockCallback(void*){return nowMs();}
Device::Config config;
nvs_handle_t storage=0;
uint8_t localMac[6]{};
struct Datagram {uint16_t length=0;uint8_t data[240]{};};
QueueHandle_t received=nullptr;
std::atomic<bool> sending{false};
std::atomic<uint32_t> radioDrops{0};
uint32_t sentAt=0;
bool radioStarted=false;
SecureSession secure;
uint32_t handshakeAt=0,retryAt=0,lastAuthenticated=0,bridge=0;
bool connected=false;
HelmetLink::Packet latestStatus{};
uint32_t statusAt=0;
#ifdef HELMET_RECEIVER
uint32_t helloAt=0;
#else
uint32_t heartbeatAt=0;
#endif
bool haveStatus=false;

// Fixed queues, no allocation or cryptography in the high-priority Wi-Fi callbacks.
void receiveCallback(const esp_now_recv_info_t *info,const uint8_t *data,int length) {
  if(!info || !info->src_addr || memcmp(info->src_addr,config.peer,6) ||
     length<=0 || length>240){++radioDrops;return;}
  Datagram d;d.length=static_cast<uint16_t>(length);memcpy(d.data,data,length);
  if(xQueueSend(received,&d,0)!=pdTRUE)++radioDrops;
}
void sendCallback(const esp_now_send_info_t*,esp_now_send_status_t result) {
  sending.store(false);if(result!=ESP_NOW_SEND_SUCCESS)++radioDrops;
}
int sendDatagram(void*,const unsigned char *data,size_t length) {
  if(length>240 || !length)return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  if(sending.exchange(true))return MBEDTLS_ERR_SSL_WANT_WRITE;
  sentAt=nowMs();
  if(esp_now_send(config.peer,data,length)!=ESP_OK){sending.store(false);return MBEDTLS_ERR_SSL_WANT_WRITE;}
  return static_cast<int>(length);
}
int receiveDatagram(void*,unsigned char *data,size_t capacity) {
  Datagram d;if(xQueueReceive(received,&d,0)!=pdTRUE)return MBEDTLS_ERR_SSL_WANT_READ;
  if(d.length>capacity){++radioDrops;return MBEDTLS_ERR_SSL_WANT_READ;}
  memcpy(data,d.data,d.length);return d.length;
}
// A queued ssl_write is retried with exactly the same buffer and length, as
// required by Mbed TLS. Latest telemetry is coalesced outside this active slot.
uint8_t secureTx[HelmetLink::MaxRaw]{};
size_t secureLength=0;
HelmetLink::Packet outgoing[4]{};
unsigned outRead=0,outWrite=0,outCount=0;
bool queueSecure(const HelmetLink::Packet &p) {
  if(outCount==4)return false;
  outgoing[outWrite]=p;outWrite=(outWrite+1)%4;++outCount;return true;
}
#ifdef HELMET_RECEIVER
HelmetLink::Decoder uartDecoder;
uint8_t uartTx[4][HelmetLink::MaxEncoded]{};
size_t uartLengths[4]{},uartSent=0;
unsigned uartRead=0,uartWrite=0,uartCount=0;
bool queueUart(const HelmetLink::Packet &p) {
  if(uartCount==4)return false;
  uint8_t raw[HelmetLink::MaxRaw];const size_t n=HelmetLink::pack(p,raw,sizeof(raw));
  if(!n)return false;
  uartLengths[uartWrite]=HelmetLink::cobsEncode(raw,n,uartTx[uartWrite],sizeof(uartTx[0]));
  if(!uartLengths[uartWrite])return false;
  uartWrite=(uartWrite+1)%4;++uartCount;return true;
}
void serviceUart(uint32_t now) {
  if(uartCount) {
    size_t n=uartLengths[uartRead]-uartSent;if(n>64)n=64;
    const int written=uart_tx_chars(UART_NUM_1,reinterpret_cast<const char*>(uartTx[uartRead]+uartSent),n);
    if(written>0)uartSent+=written;
    if(uartSent==uartLengths[uartRead]){uartSent=0;uartRead=(uartRead+1)%4;--uartCount;}
  }
  uint8_t bytes[64];const int n=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),0);
  for(int i=0;i<n;++i) {
    HelmetLink::Packet p;if(!uartDecoder.feed(bytes[i],now,p))continue;
    if(p.kind==HelmetLink::Status && p.length==HelmetLink::StatusBytes && p.epoch) {
      latestStatus=p;statusAt=now;haveStatus=true;
      WearerIO::face(p,connected && now-lastAuthenticated<HelmetLink::LinkTimeoutMs);
      if(connected && HelmetLink::u32(p.payload)==bridge)queueSecure(p);
    } else if(p.kind==HelmetLink::Ack && p.length==5 && connected && p.epoch==latestStatus.epoch &&
              HelmetLink::u32(p.payload)==bridge)queueSecure(p);
  }
  if(connected && haveStatus && now-statusAt<200 && HelmetLink::u32(latestStatus.payload)!=bridge && now-helloAt>=100) {
    HelmetLink::Packet p;p.kind=HelmetLink::Hello;p.length=4;p.epoch=latestStatus.epoch;p.ticket=latestStatus.ticket;
    HelmetLink::put32(p.payload,bridge);if(queueUart(p))helloAt=now;
  }
}
#else
HelmetLink::Packet pendingCommand{};
bool commandWaiting=false,buttonsArmed=false,comboUsed=false;
uint32_t commandAt=0,retriedAt=0,commandSequence=0,comboAt=0;
struct Button {
  int pin;bool raw=false,down=false;uint32_t changed=0;
  bool update(uint32_t now) {
    const bool value=gpio_get_level(static_cast<gpio_num_t>(pin))==0;
    if(value!=raw){raw=value;changed=now;}
    if(now-changed<30 || raw==down)return false;
    down=raw;return true;
  }
} buttons[]={{Device::NextButton},{Device::PreviousButton},{Device::BoopButton}};
void command(uint8_t action,uint8_t value,uint16_t duration,uint32_t now) {
  if(commandWaiting || !connected || !haveStatus || now-statusAt>200 || !buttonsArmed)return;
  HelmetLink::Packet p;p.kind=HelmetLink::Command;p.length=8;p.epoch=latestStatus.epoch;p.ticket=latestStatus.ticket;
  if(!++commandSequence){buttonsArmed=false;return;} // Reconnect before sequence exhaustion.
  p.sequence=commandSequence;HelmetLink::put32(p.payload,bridge);p.payload[4]=action;p.payload[5]=value;
  HelmetLink::put16(p.payload+6,duration);
  if(queueSecure(p)){pendingCommand=p;commandWaiting=true;commandAt=retriedAt=now;}
}
void serviceButtons(uint32_t now) {
  bool changed[3];for(unsigned i=0;i<3;++i)changed[i]=buttons[i].update(now);
  const bool wasArmed=buttonsArmed,hadCombo=comboAt!=0 || comboUsed;
  const bool both=buttons[0].down && buttons[1].down;
  const bool any=buttons[0].down || buttons[1].down || buttons[2].down;
  if(!connected){buttonsArmed=false;comboAt=0;comboUsed=false;commandWaiting=false;return;}
  if(!any){buttonsArmed=true;comboUsed=false;comboAt=0;}
  if(haveStatus && now-statusAt<200 && now-heartbeatAt>=500) {
    HelmetLink::Packet p;p.kind=HelmetLink::Heartbeat;p.length=4;p.epoch=latestStatus.epoch;p.ticket=latestStatus.ticket;
    HelmetLink::put32(p.payload,bridge);if(queueSecure(p))heartbeatAt=now;
  }
  if(!buttonsArmed)return;
  if(both) {
    if(!comboAt)comboAt=now;
    if(!comboUsed && !buttons[2].down && now-comboAt>=1200) {
      command(HelmetLink::Sleep,!(latestStatus.payload[5]&HelmetLink::Sleeping),0,now);comboUsed=true;
    }
  }
  if(buttons[2].down && changed[2] && !buttons[0].down && !buttons[1].down)
    command(HelmetLink::Boop,0,1200,now);
  // Navigation occurs on release so a combined hold doesn't change the face first.
  if(!comboUsed && !comboAt && !buttons[2].down) {
    if(wasArmed && !hadCombo && changed[0] && !buttons[0].down && !buttons[1].down)
      command(HelmetLink::Expression,(latestStatus.payload[4]+1)%6,0,now);
    if(wasArmed && !hadCombo && changed[1] && !buttons[1].down && !buttons[0].down)
      command(HelmetLink::Expression,(latestStatus.payload[4]+5)%6,0,now);
  }
  if(!both && any && comboAt)comboUsed=true;
  if(commandWaiting) {
    if(now-commandAt>=HelmetLink::FreshMs){commandWaiting=false;}
    else if(now-retriedAt>=100 && queueSecure(pendingCommand))retriedAt=now;
  }
}
#endif
void disconnect(uint32_t now) {
  connected=false;secureLength=outRead=outWrite=outCount=0;haveStatus=false;
#ifdef HELMET_RECEIVER
  uartCount=uartRead=uartWrite=0;uartSent=0;
#else
  commandWaiting=buttonsArmed=false;commandSequence=0;
#endif
  xQueueReset(received);secure.reset();retryAt=now+500;handshakeAt=now+500;
}
void receivedPacket(const HelmetLink::Packet &p,uint32_t now) {
#ifdef HELMET_RECEIVER
  if((p.kind==HelmetLink::Command && p.length==8) || (p.kind==HelmetLink::Heartbeat && p.length==4)) {
    if(haveStatus && now-statusAt<200 && p.epoch==latestStatus.epoch && HelmetLink::u32(p.payload)==bridge)
      queueUart(p);
  }
#else
  if(p.kind==HelmetLink::Status && p.length==HelmetLink::StatusBytes && p.epoch && HelmetLink::u32(p.payload)) {
    const uint32_t next=HelmetLink::u32(p.payload);
    if((haveStatus && p.epoch!=latestStatus.epoch) || (bridge && next!=bridge)) {
      commandWaiting=buttonsArmed=false;commandSequence=0;
    }
    bridge=next;latestStatus=p;statusAt=now;haveStatus=true;WearerIO::face(p,true);
  } else if(p.kind==HelmetLink::Ack && p.length==5 && commandWaiting && p.epoch==pendingCommand.epoch &&
            p.sequence==pendingCommand.sequence && HelmetLink::u32(p.payload)==bridge) {
    commandWaiting=false;
    printf("{\"event\":\"ack\",\"result\":%u}\n",p.payload[4]);
  }
#endif
}
void serviceRadio(uint32_t now) {
  if(!radioStarted || static_cast<int32_t>(now-retryAt)<0)return;
  if(sending.load() && now-sentAt>100){sending.store(false);++radioDrops;}
  if(!secure.ready()) {
    for(unsigned budget=0;budget<4 && !secure.ready();++budget) {
      const int r=secure.step();if(SecureSession::pending(r))break;
      if(r){disconnect(now);return;}
    }
    if(!secure.ready()){if(now-handshakeAt>5000)disconnect(now);return;}
  }
  if(!connected) {
    connected=true;lastAuthenticated=now;
#ifdef HELMET_RECEIVER
    do{esp_fill_random(&bridge,sizeof(bridge));}while(!bridge);
#endif
    printf("{\"event\":\"connected\"}\n");
  }
  for(unsigned budget=0;budget<4;++budget) {
    uint8_t bytes[HelmetLink::MaxRaw];const int r=secure.read(bytes,sizeof(bytes));
    if(SecureSession::pending(r))break;
    if(r<=0){disconnect(now);return;}
    HelmetLink::Packet p;if(HelmetLink::unpack(bytes,r,p)){lastAuthenticated=now;receivedPacket(p,now);}else ++radioDrops;
  }
  if(now-lastAuthenticated>=HelmetLink::LinkTimeoutMs){disconnect(now);return;}
  if(!secureLength && outCount) {
    secureLength=HelmetLink::pack(outgoing[outRead],secureTx,sizeof(secureTx));outRead=(outRead+1)%4;--outCount;
  }
  if(secureLength) {
    const int r=secure.write(secureTx,secureLength);
    if(r==static_cast<int>(secureLength))secureLength=0;
    else if(!SecureSession::pending(r)){disconnect(now);return;}
  }
}
bool saveConfig(const Device::Config &value) {
  uint8_t blob[Device::ConfigBytes];Device::encode(value,blob);
  return nvs_set_blob(storage,"config",blob,sizeof(blob))==ESP_OK && nvs_commit(storage)==ESP_OK;
}
bool hex(const char *s,uint8_t *bytes,size_t count) {
  if(!s || strlen(s)!=count*2)return false;
  for(size_t i=0;i<count;++i) {
    unsigned value=0;
    for(unsigned half=0;half<2;++half) {
      const char c=s[i*2+half];const int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
      if(digit<0)return false;
      value=(value<<4)|digit;
    }
    bytes[i]=static_cast<uint8_t>(value);
  }
  return true;
}
bool number(const char *s,uint32_t &v,uint32_t max) {
  if(!s || !*s)return false;
  v=0;
  while(*s){if(*s<'0'||*s>'9'||v>max/10)return false;v=v*10+(*s++-'0');if(v>max)return false;}
  return true;
}
uint32_t unlockStarted=0,unlockedAt=0;bool unlocked=false,unlockHeld=false;
void serviceUnlock(uint32_t now) {
#ifdef HELMET_RECEIVER
  const bool held=gpio_get_level(static_cast<gpio_num_t>(Device::UnlockPin))==0;
  constexpr uint32_t hold=2000;
#else
  const bool held=buttons[0].down && buttons[1].down && buttons[2].down;
  constexpr uint32_t hold=3000;
#endif
  if(held && !unlockHeld)unlockStarted=now;
  if(held && !unlocked && now-unlockStarted>=hold) {
    unlocked=true;unlockedAt=now;
    if(radioStarted)disconnect(now);
    printf("{\"event\":\"provisioning_unlocked\",\"seconds\":60}\n");
  }
  unlockHeld=held;
  if(unlocked && now-unlockedAt>=60000){unlocked=false;retryAt=now;handshakeAt=now;}
}
void consoleCommand(char *line) {
  char *save=nullptr;const char *cmd=strtok_r(line," \t",&save);
  if(!cmd)return;
  if(!strcmp(cmd,"INFO") && !strtok_r(nullptr," \t",&save)) {
    const WearerIO::Status sensors=WearerIO::snapshot();
    char pawVolts[16]="null";
    if(Device::Role==2 && (config.features&2) && sensors.pawBatteryVolts>0)
      snprintf(pawVolts,sizeof(pawVolts),"%.3f",static_cast<double>(sensors.pawBatteryVolts));
    printf("{\"ok\":true,\"role\":%u,\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"paired\":%s,\"features\":%u,\"energy_mwh\":%lu,\"drops\":%lu,\"connected\":%s,\"sensor_faults\":%u,\"paw_battery_v\":%s}\n",
      Device::Role,localMac[0],localMac[1],localMac[2],localMac[3],localMac[4],localMac[5],
      config.paired?"true":"false",config.features,static_cast<unsigned long>(config.usableEnergyMWh),static_cast<unsigned long>(radioDrops.load()),
      connected?"true":"false",sensors.faults,pawVolts);return;
  }
  if(!unlocked){puts("{\"ok\":false,\"error\":\"physical_unlock_required\"}");return;}
  Device::Config candidate=config;bool okay=false;
  if(!strcmp(cmd,"PAIR")) {
    char *peer=strtok_r(nullptr," \t",&save),*psk=strtok_r(nullptr," \t",&save),*lmk=strtok_r(nullptr," \t",&save),
      *pmk=strtok_r(nullptr," \t",&save),*channel=strtok_r(nullptr," \t",&save);uint32_t ch;
    okay=hex(peer,candidate.peer,6) && hex(psk,candidate.psk,32) && hex(lmk,candidate.lmk,16) &&
      hex(pmk,candidate.pmk,16) && number(channel,ch,11) && ch>0 && !(candidate.peer[0]&1);
    if(okay){candidate.paired=true;candidate.channel=ch;}
  } else if(!strcmp(cmd,"CONFIG")) {
    char *features=strtok_r(nullptr," \t",&save),*energy=strtok_r(nullptr," \t",&save);uint32_t f,e;
    okay=number(features,f,15) && number(energy,e,1000000);
    if(okay){candidate.features=f;candidate.usableEnergyMWh=e;}
  } else if(!strcmp(cmd,"FORGET")){candidate.paired=false;memset(candidate.psk,0,32);memset(candidate.lmk,0,16);memset(candidate.pmk,0,16);okay=true;}
  else if(!strcmp(cmd,"FULL") && !strtok_r(nullptr," \t",&save)){
    puts(WearerIO::markFull()?"{\"ok\":true}":"{\"ok\":false,\"error\":\"energy_monitor_unavailable\"}");return;
  }
  if(strtok_r(nullptr," \t",&save))okay=false;
  uint8_t blob[Device::ConfigBytes];Device::Config checked;Device::encode(candidate,blob);
  if(!okay || !Device::decode(blob,sizeof(blob),checked)){puts("{\"ok\":false,\"error\":\"invalid_configuration\"}");return;}
  if(!saveConfig(candidate)){puts("{\"ok\":false,\"error\":\"storage_failed\"}");return;}
  puts("{\"ok\":true,\"restarting\":true}");fflush(stdout);vTaskDelay(pdMS_TO_TICKS(50));esp_restart();
}
char consoleLine[192]{};size_t consoleUsed=0;bool consoleDiscard=false;uint32_t consoleAt=0;
void serviceConsole(uint32_t now) {
  if(consoleUsed && now-consoleAt>2000){consoleUsed=0;consoleDiscard=true;}
  for(unsigned budget=0;budget<64;++budget) {
    char c;if(read(STDIN_FILENO,&c,1)!=1)break;
    if(c=='\r')continue;
    if(c=='\n') {
      if(!consoleDiscard){consoleLine[consoleUsed]=0;consoleCommand(consoleLine);}
      else puts("{\"ok\":false,\"error\":\"malformed_line\"}");
      memset(consoleLine,0,sizeof(consoleLine));consoleUsed=0;consoleDiscard=false;continue;
    }
    if(!consoleUsed)consoleAt=now;
    if(!c || consoleUsed+1>=sizeof(consoleLine))consoleDiscard=true;
    if(!consoleDiscard)consoleLine[consoleUsed++]=c;
  }
}
bool startRadio() {
  if(!config.paired)return false;
  if(esp_now_init()!=ESP_OK || esp_now_set_pmk(config.pmk)!=ESP_OK)return false;
  esp_now_peer_info_t peer{};memcpy(peer.peer_addr,config.peer,6);memcpy(peer.lmk,config.lmk,16);
  peer.channel=config.channel;peer.ifidx=WIFI_IF_STA;peer.encrypt=true;
  if(esp_now_add_peer(&peer)!=ESP_OK || esp_now_register_recv_cb(receiveCallback)!=ESP_OK ||
      esp_now_register_send_cb(sendCallback)!=ESP_OK)return false;
  return secure.begin(Device::Role==1,config.psk,config.peer,{nullptr,sendDatagram,receiveDatagram,clockCallback});
}
}
extern "C" void app_main() {
  // Do not erase NVS on an error: that would silently lose the existing pair.
  if(nvs_flash_init()!=ESP_OK || nvs_open("protogen",NVS_READWRITE,&storage)!=ESP_OK){puts("Storage unavailable; wireless disabled.");return;}
  uint8_t blob[Device::ConfigBytes];size_t size=sizeof(blob);
  if(nvs_get_blob(storage,"config",blob,&size)==ESP_OK)Device::decode(blob,size,config);
  fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL,0)|O_NONBLOCK);
#ifdef HELMET_RECEIVER
  gpio_set_direction(static_cast<gpio_num_t>(Device::UnlockPin),GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(Device::UnlockPin),GPIO_PULLUP_ONLY);
  uart_config_t uart{};uart.baud_rate=115200;uart.data_bits=UART_DATA_8_BITS;uart.parity=UART_PARITY_DISABLE;
  uart.stop_bits=UART_STOP_BITS_1;uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;uart.source_clk=UART_SCLK_DEFAULT;
  if(uart_param_config(UART_NUM_1,&uart)!=ESP_OK || uart_set_pin(UART_NUM_1,Device::UartTx,Device::UartRx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK ||
      uart_driver_install(UART_NUM_1,512,0,0,nullptr,0)!=ESP_OK){puts("UART unavailable; wireless disabled.");return;}
#else
  for(const auto &b:buttons){gpio_set_direction(static_cast<gpio_num_t>(b.pin),GPIO_MODE_INPUT);gpio_set_pull_mode(static_cast<gpio_num_t>(b.pin),GPIO_PULLUP_ONLY);}
#endif
  received=xQueueCreate(8,sizeof(Datagram));if(!received){puts("Radio queue allocation failed.");return;}
  if(esp_netif_init()!=ESP_OK || esp_event_loop_create_default()!=ESP_OK)return;
  wifi_init_config_t wifi=WIFI_INIT_CONFIG_DEFAULT();
  if(esp_wifi_init(&wifi)!=ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK ||
     esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK || esp_wifi_start()!=ESP_OK)return;
  esp_wifi_get_mac(WIFI_IF_STA,localMac);
  if(esp_wifi_set_channel(config.channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK)return;
  esp_wifi_set_ps(WIFI_PS_NONE); // Prototype predictable latency first; size the paw cell from measurements.
  WearerIO::start(config);radioStarted=startRadio();handshakeAt=nowMs();
  printf("{\"event\":\"ready\",\"role\":%u,\"paired\":%s,\"radio\":%s}\n",Device::Role,config.paired?"true":"false",radioStarted?"true":"false");
  while(true) {
    const uint32_t now=nowMs();
#ifdef HELMET_RECEIVER
    serviceUart(now);
#else
    serviceButtons(now);
#endif
    serviceUnlock(now);serviceConsole(now);if(!unlocked)serviceRadio(now);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
