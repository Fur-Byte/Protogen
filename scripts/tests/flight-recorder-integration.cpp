#include "sketch.h"

static void step(unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    advanceTime(1000);
    const auto size = Serial.output.size();
    loop();
    assert(Serial.output.size() - size <= 64);
  }
}

int main(int argc, char **) {
  setup();
  step(20);
  Serial.input = "RECINFO\n";
  Serial.capacity = 0; // Accepted input, blocked output.
  const auto oldAttempts = renderMetrics.attempts;
  const auto oldAudio = audioFrames;
  const auto oldUsb = P3Teensy::timing[Flight::USB].count;
  const auto oldUart = P3Teensy::timing[Flight::UART].count;
  const auto oldUartBytes = Serial1.output.size();
  const unsigned stalled = argc > 1 ? 5000 : 400;
  step(stalled);
  assert(Serial.output.empty() && usbReplyLength > 0 && !usbReplySent);
  assert(renderMetrics.attempts > oldAttempts && audioFrames > oldAudio);
  assert(P3Teensy::timing[Flight::USB].count - oldUsb == stalled);
  assert(Serial1.output.size() > oldUartBytes);
  assert(P3Teensy::timing[Flight::UART].count - oldUart == stalled);

  Serial.capacity = 1; // Stall a record mid-line while producer keeps recording.
  step(1);
  assert(Serial.output.size() == 1 && usbReplySent == 1);
  for (unsigned i = 0; i < P3Teensy::Capacity + 16; ++i)
    selectExpression((state + 1) % ExpressionCount, millis());
  const auto overwritten = P3Teensy::ring.lost();
  assert(overwritten > 0);
  Serial.capacity = 64;
  step(20);
  // The staged line is captured before overwrite and emitted exactly once.
  assert(Serial.output.front() == '{' && Serial.output.back() == '\n');
  assert(Serial.output.find("\"overwritten\":0") != std::string::npos);
  assert(Serial.output.find('\n') == Serial.output.size() - 1);

  Serial.output.clear();
  Serial.input = "REC 0\nREC 4294967296\nRECSTAT -1\nRECINFO extra\nRECSTAT 0\n";
  step(80);
  auto replies = Serial.output;
  assert(replies.find("cursor_unavailable") != std::string::npos);
  assert(replies.find("\"bins\":[") != std::string::npos);
  unsigned errors = 0;
  for (auto at = replies.find("invalid_argument"); at != std::string::npos;
       at = replies.find("invalid_argument", at + 1)) ++errors;
  assert(errors == 3);

  Serial.output.clear();
  Serial.input = "REC " + std::to_string(P3Teensy::ring.oldest()) + "\n";
  step(20);
  assert(Serial.output.find("\"record\":[") != std::string::npos);
  assert(Serial.output.find("payload") == std::string::npos);

  Serial.output.clear();
  Serial.input = "RECINFO\n";
  Serial.capacity = 0;
  step(1);
  Serial.connected = false;
  step(25);
  assert(!usbReplyLength);
  Serial.connected = true;
  Serial.capacity = 4096;
  step(10);
  assert(Serial.output.empty());
  pauseMicrophone();
  delete sensorAdc;
  std::cout << "PASS: cursor export, bounded replies, stall/disconnect, active audio/render/UART noninterference\n";
}
