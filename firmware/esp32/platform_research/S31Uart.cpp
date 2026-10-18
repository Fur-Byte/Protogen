#include "S31Uart.hpp"
#include "driver/uart.h"
#include "soc/soc_caps.h"
namespace ProtogenResearch {
S31Uart::~S31Uart(){end();}
void S31Uart::end(){if(port>=0){uart_driver_delete(uart_port_t(port));port=-1;}}
ProfileError S31Uart::begin(const BoardProfile &p) {
  const Capabilities capabilities{SOC_GPIO_VALID_GPIO_MASK,SOC_GPIO_VALID_OUTPUT_GPIO_MASK,SOC_UART_HP_NUM};
  const auto error=validate(p,capabilities);
  if(error!=ProfileError::None)return error;
#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
  if(p.uartPort==CONFIG_ESP_CONSOLE_UART_NUM)return ProfileError::ConsoleConflict;
#endif
  // Do not remove an existing SDK driver or replace the console's ownership.
  if(port>=0 || uart_is_driver_installed(uart_port_t(p.uartPort)))return ProfileError::UartUnavailable;
  uart_config_t config{};config.baud_rate=int(p.baud);config.data_bits=UART_DATA_8_BITS;
  config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;
  config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_DEFAULT;
  const auto uart=uart_port_t(p.uartPort);
  if(uart_param_config(uart,&config)!=ESP_OK ||
     uart_driver_install(uart,512,0,0,nullptr,0)!=ESP_OK)return ProfileError::UartUnavailable;
  if(uart_set_pin(uart,p.tx,p.rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK){
    uart_driver_delete(uart);return ProfileError::PinUnavailable;
  }
  port=p.uartPort;return ProfileError::None;
}
int S31Uart::read(uint8_t *p,size_t n) {
  return port<0 || !p || n>AuthenticatedUart::ReadBudget ? -1 : uart_read_bytes(uart_port_t(port),p,n,0);
}
int S31Uart::write(const uint8_t *p,size_t n) {
  return port<0 || !p || n>AuthenticatedUart::WriteBudget ? -1 : uart_tx_chars(uart_port_t(port),reinterpret_cast<const char*>(p),n);
}
} // namespace ProtogenResearch
// Rooted only in an ignored compiler/relink probe. There is no app_main, task,
// supplied board profile or upload entry point in this research component.
extern "C" int protogen_s31_uart_research_probe(const ProtogenResearch::BoardProfile *p) {
  if(!p)return int(ProtogenResearch::ProfileError::Unreviewed);
  ProtogenResearch::S31Uart io;
  return int(io.begin(*p));
}
