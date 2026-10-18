#pragma once
#include "sdkconfig.h"
#if !defined(PROTOGEN_S31_UART_RESEARCH) || !CONFIG_IDF_TARGET_ESP32S31
#error S31_UART_RESEARCH_ONLY_NO_PRODUCTION_ADAPTER
#endif
#include "BoardProfile.hpp"
#include "AuthenticatedUart.hpp"
namespace ProtogenResearch {
// No instance is created by production sources. No profile/pins are supplied.
class S31Uart final : public ByteChannel {
  int port=-1;
public:
  ~S31Uart();
  S31Uart()=default;
  S31Uart(const S31Uart&)=delete;
  S31Uart &operator=(const S31Uart&)=delete;
  ProfileError begin(const BoardProfile &);
  void end();
  int read(uint8_t *,size_t) override;
  int write(const uint8_t *,size_t) override;
};
} // namespace ProtogenResearch
