// Host-only executable. stdout is an ordered, byte-exact trace.
#include "../tests/sketch.h"

#include <algorithm>
#if PROTOGEN_ANIMATION_TIMELINES
#include <fstream>
#endif
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
constexpr uint64_t Never = std::numeric_limits<uint64_t>::max();
constexpr unsigned PeerBacklogCapacity = 512, UsbCapacity = 512;
// Teensy core 1.62.0 Serial1 adds 256 RX bytes to its default 64.
// The serial implementation reserves one entry in each ring.
constexpr unsigned TeensyRxCapacity = 319, TeensyTxCapacity = 63;
constexpr uint64_t Baud = 115200, ByteBitsUs = 10000000;
constexpr uint64_t MaxSoakDuration = 86400000000;
uint64_t origin = 0;
bool soak = false;
uint64_t traceFrom = Never;

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
unsigned dropAck = 0, lostAcks = 0;
std::array<unsigned, HelmetLink::Unavailable + 1> acks{};
std::map<std::string, uint64_t> checks;
uint64_t queueChecks = 0, brightnessChecks = 0;
unsigned rejectedTracks = 0;
Wire toTeensy(PeerBacklogCapacity), fromTeensy(TeensyTxCapacity);
void (*adcAdvance)(uint64_t) = nullptr;
HelmetLink::Decoder peerDecoder;
HelmetLink::Packet lastStatus, lastCommand, previousCommand;
bool haveStatus = false, haveCommand = false, havePrevious = false;

struct InvariantFailure {
  std::string name;
  uint64_t expected, observed;
};

bool trace(const char *kind, bool always = false) {
  if (soak && !always && fakeUs - origin < traceFrom) return false;
  if (++traceRecords > 1000000) throw std::runtime_error("trace exceeded 1000000 records");
  std::cout << fakeUs - origin << '\t' << kind << '\t' << lastInput;
  return true;
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
  if (soak && packet[0] == 10) {
    ++brightnessChecks;
    if (SPI.registers[10] > Settings::MaxBrightness)
      throw InvariantFailure{"spi-brightness-limit", Settings::MaxBrightness, SPI.registers[10]};
  }
  if (!trace("spi")) return;
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
  if (!trace("usb")) return count;
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
    if (packet.kind == HelmetLink::Ack && packet.length == 5) {
      if (dropAck) { --dropAck; ++lostAcks; continue; }
      if (packet.payload[4] < acks.size()) ++acks[packet.payload[4]];
    }
    if (!trace("uart-packet")) continue;
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
  if (event.operation == "reorder") {
    if (!havePrevious) throw std::runtime_error("reorder requires two prior commands");
    send(previousCommand);
    return;
  }
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
    if (haveCommand) { previousCommand = lastCommand; havePrevious = true; }
    lastCommand = packet;
    haveCommand = true;
  }
  send(packet);
}
#if PROTOGEN_ANIMATION_V2
void animationInput(const Event &event) {
  using namespace Behavior::Animation;
  std::string fields = event.argument;
  std::replace(fields.begin(), fields.end(), ',', ' ');
  std::istringstream input(fields);
  if (event.operation == "v2-cancel") {
    unsigned slot, generation;
    if (!(input >> slot >> generation) || slot >= Capacity) throw std::runtime_error("invalid track handle");
    const bool cancelled = behavior.animations.cancel({static_cast<uint8_t>(slot), generation});
    if (trace("v2-cancel"))
      std::cout << '\t' << slot << '\t' << generation << '\t' << cancelled << '\n';
    return;
  }
  unsigned region, motion, priority, easing, duration;
  int from, to;
  if (!(input >> region >> motion >> priority >> easing >> duration >> from >> to) ||
      region > 3 || motion > 5 || priority > 7 || easing > 4 || duration > UINT16_MAX ||
      from < -8 || from > 8 || to < -8 || to > 8)
    throw std::runtime_error("invalid track fields");
  const auto result = behavior.animations.start(millis(), {
      static_cast<Region>(region), static_cast<Motion>(motion), static_cast<uint8_t>(priority),
      static_cast<Easing>(easing), static_cast<uint16_t>(duration),
      static_cast<int8_t>(from), static_cast<int8_t>(to)});
  if (!result.handle.generation) ++rejectedTracks;
  if (trace("v2-track"))
    std::cout << '\t' << unsigned(result.status) << '\t' << unsigned(result.handle.slot)
              << '\t' << result.handle.generation << '\n';
}
#endif

void checkpoint(const std::string &argument) {
  const auto comma = argument.find(',');
  if (comma == std::string::npos) throw std::runtime_error("malformed checkpoint");
  const auto name = argument.substr(0, comma);
  const auto expected = std::stoul(argument.substr(comma + 1));
  uint64_t observed;
  if (name == "expression") observed = state;
  else if (name == "sleeping") observed = sleeping;
  else if (name == "visor-visible") observed = visorVisible;
  else if (name == "display-blanked") observed = SPI.shutdown;
  else if (name == "effective-brightness") observed = config.effectiveBrightness();
  else if (name == "brightness-approval") observed = config.brightnessApproval;
  else if (name == "sensor-faults") observed = sensorFaults;
  else if (name == "sensor-retries") observed = sensorRetryCount();
  else if (name == "audio-retries") observed = microphoneRetryCount();
  else if (name == "audio-recovery-state") observed = microphoneRecoveryState();
  else if (name == "audio-failure-reason") observed = microphoneLastFailure();
  else if (name == "audio-running") observed = microphoneSampling;
  else if (name == "speaking") observed = speakingActive;
  else if (name == "mouth-aperture") observed = mouthAperture;
  else if (name == "boop-active") observed = boopActive;
  else if (name == "connector-ready") observed = connectorReady;
  else if (name == "dock-buffer-enabled") observed = digitalValues[Settings::DockBufferEnablePin];
  else if (name == "lost-acks") observed = lostAcks;
  else if (name == "uart-accepted") observed = acks[HelmetLink::Accepted];
  else if (name == "uart-duplicate") observed = acks[HelmetLink::Duplicate];
  else if (name == "uart-expired") observed = acks[HelmetLink::Expired];
  else if (name == "uart-invalid") observed = acks[HelmetLink::Invalid];
  else if (name == "uart-bad-session") observed = acks[HelmetLink::BadSession];
  else if (name == "rejected-tracks") observed = rejectedTracks;
#if PROTOGEN_ANIMATION_TIMELINES
  else if (name == "timeline-state") observed = static_cast<unsigned>(behavior.timeline.progress().state);
#endif
  else throw std::runtime_error("unknown checkpoint: " + name);
  if (observed != expected) throw InvariantFailure{name, expected, observed};
  ++checks[name];
}

void apply(const Event &event) {
  lastInput = event.sequence;
#if PROTOGEN_ANIMATION_TIMELINES
  if (event.operation == "timeline-cancel") {
    behavior.cancelTimeline();
    if (trace("input")) std::cout << "\ttimeline-cancel\t0\n";
    return;
  }
#endif
  if (trace("input")) std::cout << '\t' << event.operation << '\t' << event.argument << '\n';
  const auto &op = event.operation;
  if (soak && op == "expect") { checkpoint(event.argument); return; }
  if (op == "usb" || op == "uart") {
    for (auto data : unhex(event.argument)) {
      if (op == "uart") toTeensy.push(data);
      else if (Serial.input.size() == UsbCapacity) ++usbLost;
      else Serial.input.push_back(static_cast<char>(data));
    }
    usbPeak = std::max(usbPeak, static_cast<unsigned>(Serial.input.size()));
    return;
  }
#if PROTOGEN_ANIMATION_V2
  if (op == "v2-track" || op == "v2-cancel") {
    animationInput(event);
    return;
  }
#endif
  const auto value = std::stoul(event.argument);
  if (op == "visor") visorInput = value;
  else if (op == "button") buttonInput = value;
  else if (op == "boop") boopInput = value;
  else if (op == "sample") micInput = value;
  else if (op == "audio") audioAmplitude = value;
  else if (op == "sensor-stall") sensorAdc->adc1->stuck = value;
  else if (op == "audio-stall") sensorAdc->adc0->timerStalled = value;
  else if (soak && op == "audio-invalid-pin") sensorAdc->adc0->validPin = !value;
  else if (soak && op == "dock-pilot") pilotInput = value;
  else if (soak && op == "dock-voltage") voltageInput = value;
  else if (soak && op == "drop-ack") dropAck = value;
  else if (soak && op == "reorder") peer(event, value);
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
      if (soak) {
        ++queueChecks;
        if (occupied > audioBufferSize) throw InvariantFailure{"audio-queue-limit", audioBufferSize, occupied};
      }
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
void observe(bool always = false) {
  if (!always && soak && fakeUs - origin < traceFrom) return;
  std::ostringstream stateLine;
  stateLine << unsigned(state) << '\t' << sleeping << '\t' << visorVisible << '\t' << boopActive
            << '\t' << speakingActive << '\t' << sensorFaults << '\t' << audioDroppedSamples
            << '\t' << pawRejectedPackets() << '\t' << config.effectiveBrightness() + 0
            << '\t' << (pawIsOnline(millis()) ? 1 : 0)
            << '\t' << config.brightness << '\t' << config.brightnessApproval << '\t';
  for (const auto pixel : displayedFrame.bytes)
    stateLine << std::hex << std::setw(2) << std::setfill('0') << unsigned(pixel);
  const auto next = stateLine.str();
  if ((always || next != previous) && trace("state", always)) {
    std::cout << '\t' << next << '\n';
    previous = next;
  }
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

int main(int argc, char **argv) {
  try {
    int arg = 1;
    if (arg < argc && std::string(argv[arg]) == "--soak") {
      soak = true;
      ++arg;
      if (arg + 1 < argc && std::string(argv[arg]) == "--trace-from-us") {
        traceFrom = std::stoull(argv[arg + 1]);
        if (traceFrom > MaxSoakDuration) throw std::runtime_error("invalid trace start");
        arg += 2;
      }
    }
#if PROTOGEN_ANIMATION_TIMELINES
    if (argc - arg > 1) throw std::runtime_error("expected at most one asset");
#else
    if (argc != arg) throw std::runtime_error("timeline support disabled");
#endif
    uint64_t duration, tick;
    unsigned seed, epoch, accelerated, tapeLength, audioLength;
    std::string initial;
    if (!(std::cin >> origin >> duration >> tick >> seed >> epoch >> accelerated >> initial >> tapeLength >> audioLength)
        || origin > (uint64_t{1} << 48) || duration > (soak ? MaxSoakDuration : 120000000)
        || tick < 100 || tick > 20000
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
#if PROTOGEN_ANIMATION_TIMELINES
    std::array<uint8_t, Behavior::Timeline::AssetBytes> clip{};
    const bool timelineMode = argc - arg == 1;
    if (timelineMode) {
      std::ifstream stream(argv[arg], std::ios::binary);
      if (!stream.read(reinterpret_cast<char *>(clip.data()), clip.size()) || stream.peek() != EOF ||
          !Behavior::Timeline::validAsset(clip.data(), clip.size()))
        throw std::runtime_error("invalid compiled timeline");
      RuntimeConfig initialConfig;
      initialConfig.startupExpression = clip[10];
      initialConfig.naturalBlinks = clip[11];
      initialConfig.idleMotion = clip[12];
      if (!saveStoredConfig(initialConfig)) throw std::runtime_error("invalid initial configuration");
      seed = uint32_t(clip[6]) | uint32_t(clip[7]) << 8 | uint32_t(clip[8]) << 16 | uint32_t(clip[9]) << 24;
      audioAmplitude = (unsigned(clip[3]) << 8) | clip[13];
    }
#endif
    Event event;
    while (std::cin >> event.time >> event.sequence >> event.operation >> event.argument) {
      if (events.size() == (soak ? 8192 : 4096) || event.time > duration ||
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
#ifdef VisorConnector
    if (soak) SPI.requireEnabledBuffer = true;
#endif
    Serial1.spaceHook = uartSpace;
    Serial1.writeHook = uartWrite;
    Serial.writeHook = usbWrite;
    microphoneSampleHook = sample;
    setup();
    if (cursor < events.size() && events[cursor].time < fakeUs)
      throw std::runtime_error("input precedes completion of setup");
#if PROTOGEN_ANIMATION_TIMELINES
    if (timelineMode) {
      // The first opportunity is at the millisecond captured after setup.
      // Run until a loop that starts at the requested horizon has finished.
      const uint64_t boot = origin + ((fakeUs - origin + 999) / 1000) * 1000;
      if (events.size() && events.front().time < boot)
        throw std::runtime_error("input precedes the timeline origin");
      advanceTime(boot - fakeUs);
      if (!behavior.startTimeline(clip.data(), clip.size(), millis())) throw std::runtime_error("timeline admission failed");
      if (trace("timeline-origin")) std::cout << '\n';
    }
#endif
    adcAdvance = timeAdvanceHook;
    timeAdvanceHook = advanceModel;
    try { run(tick, accelerated); }
    catch (const std::runtime_error &error) {
      if (EEPROM.writeBudget != 0 || std::string(error.what()) != "simulated power loss") throw;
      if (soak) throw std::runtime_error("seed terminated by simulated power loss");
      trace("power-loss"); std::cout << '\n';
    }
#if PROTOGEN_ANIMATION_TIMELINES
    if (timelineMode && trace("timeline-final")) {
      const auto &progress = behavior.timeline.progress();
      std::cout << '\t' << unsigned(progress.state) << '\t' << progress.next << '\t'
                << progress.started << '\t' << progress.rejected << '\t' << progress.skipped << '\t';
      for (unsigned i = 0; i < progress.next; ++i) std::cout << unsigned(progress.outcomes[i]);
      std::cout << '\t' << SPI.shutdown << '\t' << SPI.transactions << '\t' << SPI.bytes
                << '\t' << renderMetrics.compositions << '\t' << renderMetrics.skipped << '\n';
    }
#endif
    if (soak) {
      observe(true);
      trace("checks", true);
      std::cout << "\taudio-queue-limit=" << queueChecks << "\tspi-brightness-limit=" << brightnessChecks;
      for (const auto &[name, count] : checks) std::cout << '\t' << name << '=' << count;
      std::cout << '\n';
      trace("soak", true);
      unsigned features = PROTOGEN_FLIGHT_RECORDER | (PROTOGEN_ANIMATION_V2 << 1)
                          | (PROTOGEN_ANIMATION_TIMELINES << 2) | (PROTOGEN_AUDIO_V2 << 3);
#ifdef VisorConnector
      features |= 1 << 4;
#endif
#if PROTOGEN_SUPERVISION_OBSERVE
      features |= 1 << 5;
#endif
#ifdef PROTOGEN_UART_AUTH
      features |= 1 << 6;
#endif
#ifdef Microphone
      features |= 1 << 7;
#endif
      std::cout << '\t' << std::min(fakeUs - origin, duration) << '\t' << features
                << '\t' << lostAcks << '\t' << acks[HelmetLink::Accepted]
                << '\t' << acks[HelmetLink::Duplicate] << '\t' << acks[HelmetLink::Expired]
                << '\t' << microphoneRetryCount() << '\t' << microphoneRecoveryCount()
                << '\t' << sensorRetryCount() << '\t' << sensorRecoveryCount()
                << '\t' << SPI.transactions << '\t' << pawRejectedPackets() << '\n';
    }
    trace("eeprom", true); hex(EEPROM.bytes.data(), 128); std::cout << '\n';
    trace("totals", true);
    std::cout << '\t' << cursor << '\t' << loops << '\t' << sampleCount << '\t' << toTeensy.peak
              << '\t' << fromTeensy.peak << '\t' << rxPeak << '\t' << usbPeak << '\t'
              << audioPeak << '\t'
              << toTeensy.dropped << '\t' << serialLost << '\t' << peerLost << '\t'
              << usbLost << '\t' << audioDroppedSamples << '\n';
    std::cerr << "skipped_loop_opportunities=" << skipped << '\n';
    pauseMicrophone();
    delete sensorAdc;
  } catch (const InvariantFailure &error) {
    std::cerr << "P8-INVARIANT\t" << fakeUs - origin << '\t' << lastInput << '\t'
              << error.name << '\t' << error.expected << '\t' << error.observed << '\n';
    return 3;
  } catch (const std::exception &error) {
    std::cerr << "event " << lastInput << " at " << fakeUs - origin << "us: " << error.what() << '\n';
    return 1;
  }
}
