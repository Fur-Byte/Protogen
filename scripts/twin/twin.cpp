// Host-only executable. stdout is an ordered, byte-exact trace.
#include "../tests/sketch.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
constexpr uint64_t Never = std::numeric_limits<uint64_t>::max();
constexpr unsigned PeerBacklogCapacity = 512, UsbCapacity = 512;
// Teensy core 1.62.0 Serial1 adds 256 RX bytes to its default 64.
// The serial implementation reserves one entry in each ring.
constexpr unsigned TeensyRxCapacity = 319, TeensyTxCapacity = 63;
constexpr uint64_t Baud = 115200, ByteBitsUs = 10000000;
uint64_t origin = 0;

struct Wire {
  struct Byte { uint64_t at; uint8_t data; };
  const unsigned capacity;
  explicit Wire(unsigned capacity) : capacity(capacity) {}
  std::deque<Byte> pending;
  uint64_t nextBitNumerator = 0;
  unsigned dropped = 0, peak = 0;
  unsigned space() const { return capacity - pending.size(); }
  void push(uint8_t byte) {
    if (!space()) { ++dropped; return; }
    nextBitNumerator = std::max(nextBitNumerator, (fakeUs - origin) * Baud) + ByteBitsUs;
    pending.push_back({origin + (nextBitNumerator + Baud - 1) / Baud, byte});
    peak = std::max(peak, static_cast<unsigned>(pending.size()));
  }
  uint64_t next() const { return pending.empty() ? Never : pending.front().at; }
  uint8_t pop() { const auto data = pending.front().data; pending.pop_front(); return data; }
};

struct Event { uint64_t time; unsigned sequence; std::string operation, argument; };
std::vector<Event> events;
std::vector<uint16_t> audioTape;
size_t cursor = 0;
uint64_t heldUntil = 0, horizon = 0;
unsigned usbLost = 0, serialLost = 0, peerLost = 0, rxPeak = 0, usbPeak = 0, audioPeak = 0;
unsigned loops = 0, sampleCount = 0, skipped = 0, shortWrite = 32, lastInput = 0;
unsigned traceRecords = 0;
bool peerConnected = true;
int audioAmplitude = 0;
Wire toTeensy(PeerBacklogCapacity), fromTeensy(TeensyTxCapacity);
void (*adcAdvance)(uint64_t) = nullptr;
HelmetLink::Decoder peerDecoder;
HelmetLink::Packet lastStatus, lastCommand;
bool haveStatus = false, haveCommand = false;

void trace(const char *kind) {
  if (++traceRecords > 1000000) throw std::runtime_error("trace exceeded 1000000 records");
  std::cout << fakeUs - origin << '\t' << kind << '\t' << lastInput;
}
void hex(const uint8_t *data, size_t size) {
  std::cout << '\t' << std::hex << std::setfill('0');
  for (size_t i = 0; i < size; ++i) std::cout << std::setw(2) << unsigned(data[i]);
  std::cout << std::dec;
}
std::vector<uint8_t> unhex(const std::string &value) {
  std::vector<uint8_t> result;
  for (size_t i = 0; i < value.size(); i += 2)
    result.push_back(static_cast<uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
  return result;
}

void latch(const std::vector<byte> &packet) {
  trace("spi");
  hex(packet.data(), packet.size());
  std::cout << '\t' << SPI.shutdown << '\t' << SPI.registers[10] + 0 << '\n';
}
int uartSpace() { return fromTeensy.space(); }
size_t uartWrite(const uint8_t *data, size_t count) {
  const size_t accepted = std::min({count, size_t(shortWrite), size_t(fromTeensy.space())});
  for (size_t i = 0; i < accepted; ++i) fromTeensy.push(data[i]);
  return accepted;
}
size_t usbWrite(const uint8_t *data, size_t count) {
  trace("usb");
  hex(data, count);
  std::cout << '\n';
  return count;
}
int sample() {
  constexpr int wave[] = {0, 1, 0, -1};
  const unsigned index = sampleCount++;
  if (index < audioTape.size()) return audioTape[index];
  return std::clamp(micInput + audioAmplitude * wave[index % 4], 0, 1023);
}
void deliver() {
  while (toTeensy.next() <= fakeUs) {
    const auto data = toTeensy.pop();
    if (!peerConnected || Serial1.input.size() == TeensyRxCapacity) ++serialLost;
    else Serial1.input.push_back(static_cast<char>(data));
    rxPeak = std::max(rxPeak, static_cast<unsigned>(Serial1.input.size()));
  }
  while (fromTeensy.next() <= fakeUs) {
    const auto data = fromTeensy.pop();
    if (!peerConnected) { ++peerLost; continue; }
    HelmetLink::Packet packet;
    if (!peerDecoder.feed(data, millis(), packet)) continue;
    if (packet.kind == HelmetLink::Status) { lastStatus = packet; haveStatus = true; }
    trace("uart-packet");
    std::cout << '\t' << unsigned(packet.kind) << '\t' << packet.sequence
              << '\t' << packet.epoch << '\t' << packet.ticket;
    hex(packet.payload, packet.length);
    std::cout << '\n';
  }
}
void send(const HelmetLink::Packet &packet) {
  uint8_t raw[HelmetLink::MaxRaw], framed[HelmetLink::MaxEncoded];
  const auto size = HelmetLink::pack(packet, raw, sizeof(raw));
  const auto count = HelmetLink::cobsEncode(raw, size, framed, sizeof(framed));
  if (!count) throw std::runtime_error("invalid peer packet");
  for (size_t i = 0; i < count; ++i) toTeensy.push(framed[i]);
}
void peer(const Event &event, unsigned value) {
  if (!haveStatus) throw std::runtime_error("peer action requires a received status");
  if (event.operation == "duplicate") {
    if (!haveCommand) throw std::runtime_error("duplicate requires a prior command");
    send(lastCommand);
    return;
  }
  HelmetLink::Packet packet;
  packet.epoch = lastStatus.epoch;
  packet.ticket = lastStatus.ticket;
  if (event.operation == "hello") {
    packet.kind = HelmetLink::Hello;
    packet.length = 4;
    HelmetLink::put32(packet.payload, value);
  } else {
    packet.kind = HelmetLink::Command;
    packet.length = 8;
    packet.sequence = event.sequence;
    const auto bridge = HelmetLink::u32(lastStatus.payload);
    if (!bridge) throw std::runtime_error("peer command requires a confirmed bridge");
    HelmetLink::put32(packet.payload, bridge);
    packet.payload[4] = HelmetLink::Expression;
    packet.payload[5] = value;
    lastCommand = packet;
    haveCommand = true;
  }
  send(packet);
}
void apply(const Event &event) {
  lastInput = event.sequence;
  trace("input");
  std::cout << '\t' << event.operation << '\t' << event.argument << '\n';
  const auto &op = event.operation;
  if (op == "usb" || op == "uart") {
    for (auto data : unhex(event.argument)) {
      if (op == "uart") toTeensy.push(data);
      else if (Serial.input.size() == UsbCapacity) ++usbLost;
      else Serial.input.push_back(static_cast<char>(data));
    }
    usbPeak = std::max(usbPeak, static_cast<unsigned>(Serial.input.size()));
    return;
  }
  const auto value = std::stoul(event.argument);
  if (op == "visor") visorInput = value;
  else if (op == "button") buttonInput = value;
  else if (op == "boop") boopInput = value;
  else if (op == "sample") micInput = value;
  else if (op == "audio") audioAmplitude = value;
  else if (op == "sensor-stall") sensorAdc->adc1->stuck = value;
  else if (op == "audio-stall") sensorAdc->adc0->timerStalled = value;
  else if (op == "hold") heldUntil = std::max(heldUntil, fakeUs + value);
  else if (op == "disconnect") {
    peerConnected = !value;
    if (value) { haveStatus = false; peerDecoder = HelmetLink::Decoder{}; }
  } else if (op == "usb-disconnect") Serial.connected = !value;
  else if (op == "short-write") shortWrite = value;
  else if (op == "power-cut-after") EEPROM.writeBudget = value;
  else if (op == "hello" || op == "expression" || op == "duplicate") peer(event, value);
  else throw std::runtime_error("unsupported event");
}

// The native ADC adapter evaluates due conversions first. Wire bytes already in
// flight are delivered next. Equal-time scripted inputs then run in file order.
void advanceModel(uint64_t target) {
  while (true) {
    const auto nextInput = cursor == events.size() ? Never : events[cursor].time;
    const auto due = std::min({nextInput, toTeensy.next(), fromTeensy.next()});
    const uint64_t stop = std::min(due, target);
    if (adcAdvance) {
      adcAdvance(stop);
      // No main-loop reads occur during this interval. The ISR uses uint16_t
      // distance and can occupy every physical slot.
      const auto occupied = static_cast<uint16_t>(audioWrite - audioRead);
      assert(occupied <= audioBufferSize);
      audioPeak = std::max(audioPeak, unsigned(occupied));
    }
    fakeUs = stop;
    deliver();
    while (cursor < events.size() && events[cursor].time == stop) apply(events[cursor++]);
    if (stop == target) return;
  }
}

std::string previous;
void observe() {
  std::ostringstream stateLine;
  stateLine << unsigned(state) << '\t' << sleeping << '\t' << visorVisible << '\t' << boopActive
            << '\t' << speakingActive << '\t' << sensorFaults << '\t' << audioDroppedSamples
            << '\t' << pawRejectedPackets() << '\t' << config.effectiveBrightness() + 0
            << '\t' << (pawIsOnline(millis()) ? 1 : 0)
            << '\t' << config.brightness << '\t' << config.brightnessApproval << '\t';
  for (const auto pixel : displayedFrame.bytes)
    stateLine << std::hex << std::setw(2) << std::setfill('0') << unsigned(pixel);
  const auto next = stateLine.str();
  if (next != previous) { trace("state"); std::cout << '\t' << next << '\n'; previous = next; }
}

void run(uint64_t tick, bool accelerate) {
  uint64_t slot = origin;
  while (slot <= horizon) {
    if (slot > fakeUs) advanceTime(slot - fakeUs);
    else advanceModel(fakeUs);
    if (fakeUs > horizon) break;
    if (fakeUs >= heldUntil) {
      ++loops;
      loop();
      observe();
    }
    // A busy loop consumes elapsed wire time. Missed opportunities are
    // coalesced rather than replayed at the same timestamp.
    slot = origin + ((fakeUs - origin) / tick + 1) * tick;
    if (accelerate && slot < heldUntil) {
      const auto input = cursor == events.size() ? Never : events[cursor].time;
      const auto boundary = std::min({heldUntil, input, horizon});
      if (boundary > slot) {
        const auto skippedSlots = (boundary - slot) / tick;
        slot += skippedSlots * tick;
        skipped += skippedSlots;
      }
    }
  }
  if (fakeUs < horizon) advanceTime(horizon - fakeUs);
}
}

int main() {
  try {
    uint64_t duration, tick;
    unsigned seed, epoch, accelerated, tapeLength, audioLength;
    std::string initial;
    if (!(std::cin >> origin >> duration >> tick >> seed >> epoch >> accelerated >> initial >> tapeLength >> audioLength)
        || origin > (uint64_t{1} << 48) || duration > 120000000 || tick < 100 || tick > 20000
        || !epoch || tapeLength > 4096 || audioLength > 32768 || (initial != "-" && initial.size() != 256))
      throw std::runtime_error("invalid runner header");
    fakeUs = origin;
    horizon = origin + duration;
    for (unsigned i = 0, word; i < tapeLength; ++i) {
      if (!(std::cin >> word)) throw std::runtime_error("incomplete cosmetic tape");
      Entropy.words.push_back(word);
    }
    for (unsigned i = 0, value; i < audioLength; ++i) {
      if (!(std::cin >> value) || value > 1023) throw std::runtime_error("invalid audio tape");
      audioTape.push_back(value);
    }
    if (initial != "-") {
      const auto bytes = unhex(initial);
      std::copy(bytes.begin(), bytes.end(), EEPROM.bytes.begin());
    }
    Event event;
    while (std::cin >> event.time >> event.sequence >> event.operation >> event.argument) {
      if (events.size() == 4096 || event.time > duration ||
          event.time < (events.empty() ? 1000 : events.back().time - origin) ||
          event.sequence != events.size() + 1)
        throw std::runtime_error("invalid runner event order or sequence");
      event.time += origin;
      events.push_back(event);
    }

    Entropy.deterministic.seed(seed);
    PawLink::gate.epoch = epoch; // Explicit host generation, separate from cosmetic draws.
    SPI.latchHook = latch;
    SPI.retainWakeFrames = false;
    Serial1.spaceHook = uartSpace;
    Serial1.writeHook = uartWrite;
    Serial.writeHook = usbWrite;
    microphoneSampleHook = sample;
    setup();
    if (cursor < events.size() && events[cursor].time < fakeUs)
      throw std::runtime_error("input precedes completion of setup");
    adcAdvance = timeAdvanceHook;
    timeAdvanceHook = advanceModel;
    try { run(tick, accelerated); }
    catch (const std::runtime_error &error) {
      if (EEPROM.writeBudget != 0 || std::string(error.what()) != "simulated power loss") throw;
      trace("power-loss"); std::cout << '\n';
    }
    trace("eeprom"); hex(EEPROM.bytes.data(), 128); std::cout << '\n';
    trace("totals");
    std::cout << '\t' << cursor << '\t' << loops << '\t' << sampleCount << '\t' << toTeensy.peak
              << '\t' << fromTeensy.peak << '\t' << rxPeak << '\t' << usbPeak << '\t'
              << audioPeak << '\t'
              << toTeensy.dropped << '\t' << serialLost << '\t' << peerLost << '\t'
              << usbLost << '\t' << audioDroppedSamples << '\n';
    std::cerr << "skipped_loop_opportunities=" << skipped << '\n';
    pauseMicrophone();
    delete sensorAdc;
  } catch (const std::exception &error) {
    std::cerr << "event " << lastInput << " at " << fakeUs - origin << "us: " << error.what() << '\n';
    return 1;
  }
}
