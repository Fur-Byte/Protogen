#include "sketch.h"

int main() {
  setup(); loop();
  char diagnostic[] = "SENSORS"; handleUsbCommand(diagnostic,millis());
  assert(strstr(usbReply,"\"adc\":{\"state\":0"));
  assert(strstr(usbReply,"\"microphone\":{\"state\":0"));
  const auto visibleWaits = controllerIdle.waits;
  for (unsigned i = 0; i < 100; ++i) loop();
  assert(controllerIdle.waits == visibleWaits);
  sleeping = true; loop();
  assert(controllerIdle.waits == visibleWaits + 1);
  assert(displayBlanked && SPI.shutdown);
  // A pending reply or buffered input is work: never park with a serial backlog.
  const auto hiddenWaits = controllerIdle.waits;
  Serial.input = std::string(130, 'x');
  loop(); assert(controllerIdle.waits == hiddenWaits && Serial.available() > 0);
  Serial.input.clear(); usbLineLength = 0; usbOverflow = false;
  Serial.input = "STATUS\n";
  loop(); assert(controllerIdle.waits == hiddenWaits);
  while (usbReplySent < usbReplyLength) loop();
  assert(Serial.output.find("\"ok\":true") != std::string::npos);
  Serial1.input = std::string(130, 'x');
  const auto uartWaits = controllerIdle.waits;
  loop(); assert(controllerIdle.waits == uartWaits && Serial1.available() > 0);
  Serial1.input.clear();
  // Poll-based button wake remains in the ordinary loop after every interrupt.
  buttonInput = LOW;
  advanceTime(1000); loop();
  advanceTime((Settings::ButtonDebounceMs + 1) * 1000); loop();
  buttonInput = HIGH;
  advanceTime(1000); loop();
  advanceTime((Settings::ButtonDebounceMs + 1) * 1000); loop();
  assert(!sleeping && !displayBlanked && !SPI.shutdown);
  const auto wakes = renderMetrics.wakes;
  advanceTime(1000); loop(); assert(renderMetrics.wakes == wakes);
  std::cout << "PASS: hidden interrupt-idle policy, USB/UART backlog, debounced wake, visible work preserved (host; no WFI/power measurement)\n";
}
