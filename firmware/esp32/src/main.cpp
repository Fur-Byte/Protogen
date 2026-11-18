#include "ActionQueue.h"
#include "ApplicationStatus.h"
#include "EspNowTransport.h"
#include "PairingIndicator.h"
#include "SecureStorage.h"
#include "WearerIO.h"
#include "Efficiency.h"
#include "WirelessLink.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static_assert(configTICK_RATE_HZ==1000, "Production bounded poll requires pinned1000Hz RTOS tick");
#if PROTOGEN_FLIGHT_RECORDER
// app_main is the sole owner. Callback counts already use atomics. Wearer
// metrics are published as a coherent copy through its existing portMUX.
extern "C" {
Flight::Record p3C3Records[128];
Flight::Ring<128> p3C3Ring(p3C3Records);
Flight::Timing p3C3Timings[Flight::StageCount];
Flight::Rate p3C3Rate;
}
namespace P3C3 {
uint32_t clockUs() { return static_cast<uint32_t>(esp_timer_get_time()); }
void emit(uint32_t at, Flight::Event event, uint16_t detail, uint32_t value) {
  Flight::Scope cost(p3C3Timings[Flight::Append], clockUs);
  p3C3Ring.append(at, event, detail, value);
}
}
#define P3_C3_SCOPE(variable, stage) Flight::Scope variable(p3C3Timings[Flight::stage], P3C3::clockUs)
#define P3_C3_CALL(stage, expression) do { P3_C3_SCOPE(p3Call, stage); expression; } while (false)
#else
#define P3_C3_SCOPE(variable, stage)
#define P3_C3_CALL(stage, expression) do { expression; } while (false)
#endif

namespace {
uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
Device::Config config;
nvs_handle_t storage = 0;
Wireless::EspNowTransport transport;
void connectionChanged(void *, bool);
void wirelessPacket(void *, const HelmetLink::Packet &);
bool storePair(void *, const uint8_t[32]);
Wireless::Link wirelessLink(transport, Device::Role,
                            {nullptr, connectionChanged, wirelessPacket,
                             storePair});
Wireless::Indicator indicator;
bool radioStarted = false, connected = false;
TaskHandle_t mainTask = nullptr;
uint32_t mainExecutions = 0, mainMaxUs = 0;
uint32_t uartQueueMaxMs=0, uartQueueFull=0, intentQueueMaxMs=0, commandAckMaxMs=0;
uint64_t mainWorkUs = 0;
void buttonEdge(void *) {
  BaseType_t wake = pdFALSE;
  vTaskNotifyGiveFromISR(mainTask, &wake);
  if (wake) portYIELD_FROM_ISR();
}
#ifdef HELMET_RECEIVER
QueueHandle_t uartEvents = nullptr;
void uartEventTask(void *) {
  uart_event_t event;
  while (true) if (xQueueReceive(uartEvents, &event, portMAX_DELAY) == pdTRUE)
    xTaskNotifyGive(mainTask);
}
#endif
Wireless::HelmetStatus helmetStatus;
#ifdef HELMET_RECEIVER
uint32_t helloAt = 0, uartHeartbeatAt = 0, statusSentAt = 0;
HelmetLink::Packet lastSentStatus{};
Wireless::UartStatusFence statusFence;
#endif
bool queueSecure(const HelmetLink::Packet &p) {
#ifdef HELMET_RECEIVER
  if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes) {
    const auto sensors = WearerIO::snapshot();
    return wirelessLink.queue(Wireless::extendHelmetStatus(
        p, sensors.remainingPercent, sensors.faults));
  }
#endif
  return wirelessLink.queue(p);
}
#ifdef HELMET_RECEIVER
#ifdef PROTOGEN_UART_AUTH
#include "UartAuthentication.h"
#include <ProtogenUartKey.h>
bool uartMac(void *, const uint8_t *p, size_t n, uint8_t out[32]) {
  return Wireless::Crypto::hmac(PROTOGEN_UART_AUTH_KEY, p, n, out);
}
bool uartRandom(void *, uint8_t *p, size_t n) {
  return Wireless::Crypto::random(p, n);
}
UartSecurity::Endpoint uartDecoder(UartSecurity::Esp, nullptr, uartMac,
                                   uartRandom);
constexpr size_t UartMaxEncoded = UartSecurity::MaxEncoded;
bool uartAuthStarted = false;
#else
HelmetLink::Decoder uartDecoder;
constexpr size_t UartMaxEncoded = HelmetLink::MaxEncoded;
#endif
uint8_t uartTx[4][UartMaxEncoded]{};
size_t uartLengths[4]{}, uartSent = 0;
uint32_t uartQueuedAt[4]{};
unsigned uartRead = 0, uartWrite = 0, uartCount = 0;
bool queueUart(const HelmetLink::Packet &p) {
  if (uartCount == 4) {Efficiency::increment(uartQueueFull);return false;}
#ifdef PROTOGEN_UART_AUTH
  uartLengths[uartWrite] =
      uartDecoder.seal(p, uartTx[uartWrite], sizeof(uartTx[0]));
#else
  uint8_t raw[HelmetLink::MaxRaw];
  const size_t n = HelmetLink::pack(p, raw, sizeof(raw));
  if (!n)
    return false;
  uartLengths[uartWrite] =
      HelmetLink::cobsEncode(raw, n, uartTx[uartWrite], sizeof(uartTx[0]));
#endif
  if (!uartLengths[uartWrite])
    return false;
  uartQueuedAt[uartWrite]=nowMs();
  uartWrite = (uartWrite + 1) % 4;
  ++uartCount;
  return true;
}
void serviceUart(uint32_t now) {
  P3_C3_SCOPE(p3Uart, UART);
  if(statusFence.poll(now))helmetStatus.available=false;
#ifdef PROTOGEN_UART_AUTH
  if (!uartAuthStarted)
    uartAuthStarted = uartDecoder.begin(0, now);
  if (!uartCount) {
    const size_t n =
        uartDecoder.poll(now, uartTx[uartWrite], sizeof(uartTx[0]));
    if (n) {
      uartLengths[uartWrite] = n;
      uartQueuedAt[uartWrite]=now;
      uartWrite = (uartWrite + 1) % 4;
      ++uartCount;
    }
  }
#endif
  if (uartCount) {
    size_t n = uartLengths[uartRead] - uartSent;
    if (n > 64)
      n = 64;
    const int written = uart_tx_chars(
        UART_NUM_1, reinterpret_cast<const char *>(uartTx[uartRead] + uartSent),
        n);
    if (written > 0)
      uartSent += written;
    if (uartSent == uartLengths[uartRead]) {
      const uint32_t queueMs=now-uartQueuedAt[uartRead];
      if(queueMs>uartQueueMaxMs)uartQueueMaxMs=queueMs;
      uartSent = 0;
      uartRead = (uartRead + 1) % 4;
      --uartCount;
    }
  }
  uint8_t bytes[64];
  const int n = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), 0);
  for (int i = 0; i < n; ++i) {
    HelmetLink::Packet p;
    if (!uartDecoder.feed(bytes[i], now, p))
      continue;
    if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes &&
        p.epoch) {
      const bool postAck=statusFence.awaitingStatus();
      const bool usable=statusFence.status(p);
      helmetStatus.packet = p;
      helmetStatus.at = now;
      helmetStatus.available = usable;
      WearerIO::face(p, connected);
      if (usable && connected && HelmetLink::u32(p.payload) == helmetStatus.bridge &&
          (postAck || (now - statusSentAt >= Wireless::ActivePollMs &&
           (memcmp(p.payload + 4, lastSentStatus.payload + 4, 4) ||
            memcmp(p.payload + 10, lastSentStatus.payload + 10, 2))))) {
        if (queueSecure(p)) {
          lastSentStatus = p;
          statusSentAt = now;
        }
      }
    } else if (p.kind == HelmetLink::Ack && p.length == 5 && connected &&
               p.epoch == helmetStatus.packet.epoch &&
               HelmetLink::u32(p.payload) == helmetStatus.bridge) {
      statusFence.ack(p);queueSecure(p);
    }
  }
  // Wired Teensy liveness causes no RF traffic.
  if (connected && helmetStatus.available &&
      now - helmetStatus.at < Wireless::FreshStatusMs &&
      HelmetLink::u32(helmetStatus.packet.payload) == helmetStatus.bridge &&
      now - uartHeartbeatAt >= Wireless::UartHeartbeatMs) {
    HelmetLink::Packet p;
    p.kind = HelmetLink::Heartbeat;
    p.length = 4;
    p.epoch = helmetStatus.packet.epoch;
    p.ticket = helmetStatus.packet.ticket;
    HelmetLink::put32(p.payload, helmetStatus.bridge);
    if (queueUart(p))
      uartHeartbeatAt = now;
  }
  if (connected && helmetStatus.available &&
      now - helmetStatus.at < Wireless::FreshStatusMs &&
      HelmetLink::u32(helmetStatus.packet.payload) != helmetStatus.bridge &&
      now - helloAt >= Wireless::ActivePollMs) {
    HelmetLink::Packet p;
    p.kind = HelmetLink::Hello;
    p.length = 4;
    p.epoch = helmetStatus.packet.epoch;
    p.ticket = helmetStatus.packet.ticket;
    HelmetLink::put32(p.payload, helmetStatus.bridge);
    if (queueUart(p))
      helloAt = now;
  }
}
#else
Wireless::Actions actions;
HelmetLink::Packet pendingCommand{};
bool commandWaiting = false, buttonsArmed = false, comboUsed = false;
uint32_t commandAt = 0, retriedAt = 0, commandSequence = 0, comboAt = 0;
struct Button {
  int pin;
  bool raw = false, down = false;
  uint32_t changed = 0;
  bool update(uint32_t now) {
    const bool value = gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
    if (value != raw) {
      raw = value;
      changed = now;
    }
    if (now - changed < Wireless::DebounceMs || raw == down)
      return false;
    down = raw;
    return true;
  }
} buttons[] = {
    {Device::NextButton}, {Device::PreviousButton}, {Device::BoopButton}};
void command(uint8_t action, uint8_t value, uint16_t duration, uint32_t now) {
  if (!connected || !buttonsArmed)
    return;
  if (actions.push(action, value, duration, now))
    wirelessLink.activity(now);
}
void serviceCommands(uint32_t now) {
  if (commandWaiting) {
    if (now - commandAt >= HelmetLink::FreshMs)
      commandWaiting = false;
    else if (now - retriedAt >= Wireless::CommandRetryMs &&
             queueSecure(pendingCommand))
      retriedAt = now;
    return;
  }
  if (!actions.pending() || !connected || !helmetStatus.available ||
      now - helmetStatus.at >= Wireless::FreshStatusMs)
    return;
  Wireless::Intent intent;
  if (!actions.pop(now, intent))
    return;
  if (intent.action == Wireless::Actions::Next ||
      intent.action == Wireless::Actions::Previous) {
    intent.value = (helmetStatus.packet.payload[4] +
                    (intent.action == Wireless::Actions::Next ? 1 : 5)) % 6;
    intent.action = HelmetLink::Expression;
  } else if (intent.action == Wireless::Actions::ToggleSleep) {
    intent.action = HelmetLink::Sleep;
    intent.value = !(helmetStatus.packet.payload[5] & HelmetLink::Sleeping);
  }
  if (commandSequence == UINT32_MAX) {
    buttonsArmed = false;
    actions.clear();
    return;
  }
  HelmetLink::Packet p;
  p.kind = HelmetLink::Command;
  p.length = 8;
  p.epoch = helmetStatus.packet.epoch;
  p.ticket = helmetStatus.packet.ticket;
  p.sequence = ++commandSequence;
  HelmetLink::put32(p.payload, helmetStatus.bridge);
  p.payload[4] = intent.action;
  p.payload[5] = intent.value;
  HelmetLink::put16(p.payload + 6, intent.duration);
  if (queueSecure(p)) {
    const uint32_t queueMs=now-intent.at;
    if(queueMs>intentQueueMaxMs)intentQueueMaxMs=queueMs;
    pendingCommand = p;
    commandWaiting = true;
    commandAt = retriedAt = now;
  }
}
void serviceButtons(uint32_t now) {
  bool changed[3];
  for (unsigned i = 0; i < 3; ++i)
    changed[i] = buttons[i].update(now);
  const bool wasArmed = buttonsArmed, hadCombo = comboAt != 0 || comboUsed;
  const bool both = buttons[0].down && buttons[1].down;
  const bool any = buttons[0].down || buttons[1].down || buttons[2].down;
  if (!connected) {
    buttonsArmed = false;
    comboAt = 0;
    comboUsed = false;
    commandWaiting = false;
    actions.clear();
    return;
  }
  if (!any) {
    buttonsArmed = true;
    comboUsed = false;
    comboAt = 0;
  }
  if (any || commandWaiting || actions.pending())
    wirelessLink.activity(now);
  if (!buttonsArmed)
    return;
  if (both) {
    if (!comboAt)
      comboAt = now;
    if (!comboUsed && !buttons[2].down &&
        now - comboAt >= Wireless::SleepComboMs) {
      command(Wireless::Actions::ToggleSleep, 0, 0, now);
      comboUsed = true;
    }
  }
  if (buttons[2].down && changed[2] && !buttons[0].down && !buttons[1].down)
    command(HelmetLink::Boop, 0, Wireless::BoopDurationMs, now);
  // Navigation occurs on release so a combined hold doesn't change the face
  // first.
  if (!comboUsed && !comboAt && !buttons[2].down) {
    if (wasArmed && !hadCombo && changed[0] && !buttons[0].down &&
        !buttons[1].down)
      command(Wireless::Actions::Next, 0, 0, now);
    if (wasArmed && !hadCombo && changed[1] && !buttons[1].down &&
        !buttons[0].down)
      command(Wireless::Actions::Previous, 0, 0, now);
  }
  if (!both && any && comboAt)
    comboUsed = true;
  serviceCommands(now);
}
#endif
void connectionChanged(void *, bool online) {
  connected = online;
#ifdef HELMET_RECEIVER
  uartCount = uartRead = uartWrite = 0;
  uartSent = 0;statusFence.clear();
  if (online) {
    do {
      Wireless::Crypto::random(&helmetStatus.bridge, sizeof(helmetStatus.bridge));
    } while (!helmetStatus.bridge);
    lastSentStatus = HelmetLink::Packet{};
  }
  WearerIO::link(online);
#else
  commandWaiting = buttonsArmed = false;
  commandSequence = 0;
  actions.clear();
  helmetStatus.available = false;
  helmetStatus.bridge = 0;helmetStatus.expecting=false;
  WearerIO::link(online);
#endif
  printf("{\"event\":\"%s\"}\n", online ? "connected" : "disconnected");
}
bool storePair(void *, const uint8_t secret[32]) {
  Device::Config candidate = config;
  candidate.paired = true;
  memcpy(candidate.psk, secret, 32);
  const bool okay = SecureStorage::save(storage, candidate);
  if (okay)
    config = candidate;
  Wireless::Crypto::wipe(candidate.psk, 32);
  return okay;
}
void wirelessPacket(void *, const HelmetLink::Packet &p) {
  const uint32_t now = nowMs();
#ifdef HELMET_RECEIVER
  if (p.kind == HelmetLink::Status && p.length == 4)
    return; // authenticated paw cell/fault telemetry
  if (p.kind == HelmetLink::Status && p.length == 0) {
    if (helmetStatus.available &&
        now - helmetStatus.at < Wireless::FreshStatusMs &&
        HelmetLink::u32(helmetStatus.packet.payload) == helmetStatus.bridge)
      queueSecure(helmetStatus.packet);
    return;
  }
  if ((p.kind == HelmetLink::Command && p.length == 8) ||
      (p.kind == HelmetLink::Heartbeat && p.length == 4)) {
    if ((helmetStatus.available || statusFence.retry(p)) &&
        now - helmetStatus.at < Wireless::FreshStatusMs &&
        p.epoch == helmetStatus.packet.epoch &&
        HelmetLink::u32(p.payload) == helmetStatus.bridge)
      if(queueUart(p) && p.kind==HelmetLink::Command) {
        statusFence.queued(p,now);helmetStatus.available=false;
      }
  }
#else
  bool changed = false;
  if (helmetStatus.receive(p, now, changed)) {
    if (changed) {
      commandWaiting = buttonsArmed = false;
      commandSequence = 0;
      actions.clear();
    }
    WearerIO::face(p, true);
  } else if (p.kind == HelmetLink::Ack && p.length == 5 && commandWaiting &&
             p.epoch == pendingCommand.epoch &&
             p.sequence == pendingCommand.sequence &&
             HelmetLink::u32(p.payload) == helmetStatus.bridge) {
    const uint32_t ackMs=now-commandAt;
    if(ackMs>commandAckMaxMs)commandAckMaxMs=ackMs;
    commandWaiting = false;
    helmetStatus.expectAfterAck(pendingCommand,p.payload[4],now);
    wirelessLink.refreshStatus(now);
    printf("{\"event\":\"ack\",\"result\":%u}\n", p.payload[4]);
  }
#endif
}
bool number(const char *s, uint32_t &v, uint32_t max) {
  if (!s || !*s)
    return false;
  v = 0;
  while (*s) {
    if (*s < '0' || *s > '9' || v > max / 10)
      return false;
    v = v * 10 + (*s++ - '0');
    if (v > max)
      return false;
  }
  return true;
}
uint32_t unlockStarted = 0, unlockedAt = 0;
bool unlocked = false, unlockHeld = false, confirmationArmed = false,
     entryConsumed = false, eraseConsumed = false;
void serviceUnlock(uint32_t now) {
#ifdef HELMET_RECEIVER
  const bool held =
      gpio_get_level(static_cast<gpio_num_t>(Device::UnlockPin)) == 0;
  constexpr uint32_t hold = Wireless::HelmetPairHoldMs;
#else
  const bool held = buttons[0].down && buttons[1].down && buttons[2].down;
  constexpr uint32_t hold = Wireless::PawPairHoldMs;
#endif
  if (held && !unlockHeld)
    unlockStarted = now;
  if (!held) {
    entryConsumed = eraseConsumed = false;
    // Confirmation is armed below, only after the code has been fully shown.
  }
  if (held && !unlocked && !entryConsumed && now - unlockStarted >= hold) {
    unlocked = true;
    unlockedAt = now;
    entryConsumed = true;
    confirmationArmed = false;
    if (radioStarted)
      wirelessLink.startPairing(now);
    puts("{\"event\":\"wireless_pairing\",\"seconds\":60}");
  }
  if (unlocked && held && !eraseConsumed &&
      now - unlockStarted >= Wireless::EraseHoldMs) {
    Device::Config cleared = config;
    cleared.paired = false;
    Wireless::Crypto::wipe(cleared.psk, 32);
    if (SecureStorage::save(storage, cleared)) {
      puts("{\"event\":\"pair_erased\"}");
      fflush(stdout);
      esp_restart();
    }
    eraseConsumed = true;
  }
  uint32_t code = 0;
  const bool codePresented = unlocked && wirelessLink.pairingCode(code) &&
                             indicator.complete(now, code);
  // A complete six-digit LED cycle must precede a fresh release and hold.
  // Do not accept a pre-held button when the first display finishes.
  if (!codePresented)
    confirmationArmed = false;
  else if (!held)
    confirmationArmed = true;
  if (codePresented && confirmationArmed && held &&
      now - unlockStarted >= Wireless::PairConfirmHoldMs) {
    wirelessLink.confirmPairing();
    confirmationArmed = false;
  }
  if (unlocked && now - unlockedAt >= Wireless::PairingMs)
    unlocked = false;
  unlockHeld = held;
}
void consoleCommand(char *line) {
  char *save = nullptr;
  const char *cmd = strtok_r(line, " \t", &save);
  if (!cmd)
    return;
  if (!strcmp(cmd, "LATENCY") && !strtok_r(nullptr, " \t", &save)) {
    printf("{\"ok\":true,\"role\":%u,\"tick_hz\":%u,\"service_poll_ms\":%u,\"intent_queue_max_ms\":%lu,"
      "\"uart_queue_max_ms\":%lu,\"uart_queue_full\":%lu,\"command_ack_max_ms\":%lu,\"main_max_us\":%lu}\n",
      Device::Role,unsigned(configTICK_RATE_HZ),unsigned(Wireless::ServicePollMs),static_cast<unsigned long>(intentQueueMaxMs),
      static_cast<unsigned long>(uartQueueMaxMs),static_cast<unsigned long>(uartQueueFull),
      static_cast<unsigned long>(commandAckMaxMs),static_cast<unsigned long>(mainMaxUs));
    return;
  }
  if (!strcmp(cmd, "ENERGY") && !strtok_r(nullptr, " \t", &save)) {
    const auto s=WearerIO::snapshot();
    printf("{\"ok\":true,\"rail\":\"upstream_pd_input\",\"enabled\":%s,\"valid\":%s,"
      "\"fault\":%u,\"samples\":%lu,\"last_valid_ms\":%lu,\"failures\":%lu,\"recoveries\":%lu,"
      "\"volts\":%.3f,\"amps\":%.4f,\"measured_mwh\":%.6f,\"soc_qualified\":%s}\n",
      s.energyEnabled?"true":"false",s.energyValid?"true":"false",unsigned(s.energyFault),
      static_cast<unsigned long>(s.energySamples),static_cast<unsigned long>(s.energyValidAt),
      static_cast<unsigned long>(s.energyFailures),static_cast<unsigned long>(s.energyRecoveries),
      static_cast<double>(s.volts),static_cast<double>(s.amps),s.measuredMWh,s.estimateQualified?"true":"false");
    return;
  }
  if (!strcmp(cmd, "PERF") && !strtok_r(nullptr, " \t", &save)) {
    const auto s = WearerIO::snapshot();
    const auto &m = s.metrics;
    const auto writes = SecureStorage::metrics();
    printf("{\"ok\":true,\"efficiency_flags\":%u,\"reset_reason\":%u,"
           "\"heap_free\":%lu,\"heap_minimum\":%lu,\"main_executions\":%lu,"
           "\"main_work_us\":%llu,\"main_max_us\":%lu,\"radio_tx_attempts\":%lu,"
           "\"radio_tx_completions\":%lu,\"radio_rx_packets\":%lu,\"radio_drops\":%lu,"
           "\"wearer_executions\":%lu,\"wearer_work_us\":%llu,\"wearer_max_us\":%lu,"
           "\"case_reads\":%lu,\"environment_reads\":%lu,\"energy_reads\":%lu,"
           "\"oled_pages\":%lu,\"oled_bytes\":%lu,\"checkpoint_attempts\":%lu,"
           "\"checkpoint_commits\":%lu,\"checkpoint_failures\":%lu,"
           "\"measured_mwh\":%.6f,\"measured_intervals\":%lu,\"energy_valid\":%s,"
           "\"used_mwh\":%.3f,\"soc_qualified\":%s,\"case_max_c\":%.2f,"
           "\"environment_valid\":%s,\"case_valid\":%s,\"temperature_c\":%.2f,"
           "\"humidity_percent\":%.2f,\"case_c\":%.2f,\"volts\":%.3f,\"amps\":%.4f,"
           "\"config_write_attempts\":%lu,\"config_commits\":%lu,\"config_verified\":%lu,\"config_write_failures\":%lu}\n",
           Efficiency::Flags, unsigned(esp_reset_reason()),
           static_cast<unsigned long>(esp_get_free_heap_size()),
           static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
           static_cast<unsigned long>(mainExecutions), static_cast<unsigned long long>(mainWorkUs),
           static_cast<unsigned long>(mainMaxUs), static_cast<unsigned long>(transport.sends()),
           static_cast<unsigned long>(transport.completions()), static_cast<unsigned long>(transport.receives()),
           static_cast<unsigned long>(transport.dropped()), static_cast<unsigned long>(m.executions),
           static_cast<unsigned long long>(m.workUs), static_cast<unsigned long>(m.maxWorkUs),
           static_cast<unsigned long>(m.caseReads), static_cast<unsigned long>(m.environmentReads),
           static_cast<unsigned long>(m.energyReads), static_cast<unsigned long>(m.oledPages),
           static_cast<unsigned long>(m.oledBytes), static_cast<unsigned long>(m.commitAttempts),
           static_cast<unsigned long>(m.commits), static_cast<unsigned long>(m.commitFailures),
           s.measuredMWh, static_cast<unsigned long>(s.measuredIntervals), s.energyValid?"true":"false",
           s.usedMWh,s.estimateQualified?"true":"false",static_cast<double>(s.maximumCaseTemperature),
           s.environmentValid?"true":"false",s.caseValid?"true":"false",
           static_cast<double>(s.temperature),static_cast<double>(s.humidity),
           static_cast<double>(s.caseTemperature),static_cast<double>(s.volts),static_cast<double>(s.amps),
           static_cast<unsigned long>(writes.attempts),static_cast<unsigned long>(writes.commits),
           static_cast<unsigned long>(writes.verified),static_cast<unsigned long>(writes.failures));
    return;
  }
  if (!strcmp(cmd, "SECURITY")) {
    const char *sub = strtok_r(nullptr, " \t", &save);
    if (sub && !strcmp(sub, "STATUS") && !strtok_r(nullptr, " \t", &save)) {
      const auto s = wirelessLink.securityStatus(
          static_cast<uint32_t>(esp_timer_get_time() / 1000));
      const auto &d = s.counters;
      printf("{\"ok\":true,\"state\":\"%s\",\"age_ms\":%lu,\"records\":%llu,"
             "\"renewal_ms\":%lu,\"channel\":%u,"
             "\"aead_failures\":%lu,\"replay_rejects\":%lu,\"malformed\":%lu,"
             "\"versions\":%lu,"
             "\"pairing_failures\":%lu,\"pairing_timeouts\":%lu,\"handshake_"
             "failures\":%lu,\"finished_failures\":%lu,"
             "\"recoveries\":%lu,\"renewals\":%lu,\"channel_recoveries\":%lu,"
             "\"identity_rotations\":%lu,\"rate_drops\":%lu,\"disconnects\":%"
             "lu}\n",
             Wireless::stateName(s.state), static_cast<unsigned long>(s.ageMs),
             static_cast<unsigned long long>(s.records),
             static_cast<unsigned long>(s.renewalInMs), s.channel,
             static_cast<unsigned long>(d.aeadFailures),
             static_cast<unsigned long>(d.replayRejects),
             static_cast<unsigned long>(d.malformed),
             static_cast<unsigned long>(d.versions),
             static_cast<unsigned long>(d.pairingFailures),
             static_cast<unsigned long>(d.pairingTimeouts),
             static_cast<unsigned long>(d.handshakeFailures),
             static_cast<unsigned long>(d.finishedFailures),
             static_cast<unsigned long>(d.recoveries),
             static_cast<unsigned long>(d.renewals),
             static_cast<unsigned long>(d.channelRecoveries),
             static_cast<unsigned long>(d.identityRotations),
             static_cast<unsigned long>(d.rateDrops),
             static_cast<unsigned long>(d.disconnects));
      return;
    }
  }
  if (!strcmp(cmd, "INFO") && !strtok_r(nullptr, " \t", &save)) {
    const WearerIO::Status sensors = WearerIO::snapshot();
    char pawVolts[16] = "null";
    if (Device::Role == 2 && (config.features & 2) &&
        sensors.pawBatteryVolts > 0)
      snprintf(pawVolts, sizeof(pawVolts), "%.3f",
               static_cast<double>(sensors.pawBatteryVolts));
    printf("{\"ok\":true,\"role\":%u,\"paired\":%s,\"features\":%u,\"energy_"
           "mwh\":%lu,\"drops\":%lu,\"connected\":%s,\"sensor_faults\":%u,"
           "\"paw_battery_v\":%s}\n",
           Device::Role, config.paired ? "true" : "false", config.features,
           static_cast<unsigned long>(config.usableEnergyMWh),
           static_cast<unsigned long>(transport.dropped()),
           connected ? "true" : "false", sensors.faults, pawVolts);
    return;
  }
  if (!unlocked) {
    puts("{\"ok\":false,\"error\":\"physical_unlock_required\"}");
    return;
  }
  Device::Config candidate = config;
  bool okay = false;
  if (!strcmp(cmd, "CONFIG")) {
    char *features = strtok_r(nullptr, " \t", &save),
         *energy = strtok_r(nullptr, " \t", &save);
    uint32_t f, e;
    okay = number(features, f, 15) && number(energy, e, 1000000);
    if (okay) {
      candidate.features = f;
      candidate.usableEnergyMWh = e;
    }
  } else if (!strcmp(cmd, "FORGET")) {
    candidate.paired = false;
    memset(candidate.psk, 0, 32);
    okay = true;
  } else if (!strcmp(cmd, "FULL") && !strtok_r(nullptr, " \t", &save)) {
    puts(WearerIO::markFull()
             ? "{\"ok\":true}"
             : "{\"ok\":false,\"error\":\"energy_monitor_unavailable\"}");
    return;
  }
  if (strtok_r(nullptr, " \t", &save))
    okay = false;
  uint8_t blob[Device::ConfigBytes];
  Device::Config checked;
  Device::encode(candidate, blob);
  const bool decoded = Device::decode(blob, sizeof(blob), checked);
  Wireless::Crypto::wipe(blob, sizeof(blob));
  if (!okay || !decoded) {
    puts("{\"ok\":false,\"error\":\"invalid_configuration\"}");
    return;
  }
  if (!SecureStorage::save(storage, candidate)) {
    puts("{\"ok\":false,\"error\":\"storage_failed\"}");
    return;
  }
  Wireless::Crypto::wipe(candidate.psk, sizeof(candidate.psk));
  Wireless::Crypto::wipe(checked.psk, sizeof(checked.psk));
  puts("{\"ok\":true,\"restarting\":true}");
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(50));
  esp_restart();
}
char consoleLine[192]{};
size_t consoleUsed = 0;
bool consoleDiscard = false;
uint32_t consoleAt = 0;
void serviceConsole(uint32_t now) {
  P3_C3_SCOPE(p3Console, USB);
  if (consoleUsed && now - consoleAt > 2000) {
    consoleUsed = 0;
    consoleDiscard = true;
  }
  for (unsigned budget = 0; budget < 64; ++budget) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      break;
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (!consoleDiscard) {
        consoleLine[consoleUsed] = 0;
        consoleCommand(consoleLine);
      } else
        puts("{\"ok\":false,\"error\":\"malformed_line\"}");
      memset(consoleLine, 0, sizeof(consoleLine));
      consoleUsed = 0;
      consoleDiscard = false;
      continue;
    }
    if (!consoleUsed)
      consoleAt = now;
    if (!c || consoleUsed + 1 >= sizeof(consoleLine))
      consoleDiscard = true;
    if (!consoleDiscard)
      consoleLine[consoleUsed++] = c;
  }
}
#if PROTOGEN_FLIGHT_RECORDER
Flight::Flood p3Gap;

void sampleP3(uint32_t nowMs) {
  static Flight::Flood drops, queueFull, replay, aead, wearerFaults;
  static bool wasConnected = false;
  static uint32_t priorSample = 0, lastHeap = UINT32_MAX, lastStack = UINT32_MAX;
  static uint32_t lastWorkerStack = UINT32_MAX, lastWorkerWork = 0, lastOled = 0;
  static uint32_t lastEnergy = 0, lastEnvironment = 0, lastCase = 0;
  const uint32_t now = P3C3::clockUs();
  if (wasConnected != connected) {
    P3C3::emit(now, Flight::RadioLink, connected ? 1 : 0, 0);
    wasConnected = connected;
  }
  drops.count(now, transport.dropped());
  queueFull.count(now, uartQueueFull);
  drops.flush(now, Flight::RadioDrops, P3C3::emit);
  queueFull.flush(now, Flight::UartQueueFull, P3C3::emit);
  p3Gap.flush(now, Flight::MainGap, P3C3::emit);
  p3C3Rate.sample(now, p3C3Ring.next());
  if (nowMs - priorSample < 1000) return;
  priorSample = nowMs;

  const auto status = wirelessLink.securityStatus(nowMs);
  replay.count(now, status.counters.replayRejects);
  aead.count(now, status.counters.aeadFailures);
  replay.flush(now, Flight::ReplayReject, P3C3::emit);
  aead.flush(now, Flight::AeadReject, P3C3::emit);
  const auto worker = WearerIO::snapshot();
  wearerFaults.state(now, worker.faults);
  wearerFaults.flush(now, Flight::SensorFault, P3C3::emit);
  const auto update = [now](Flight::Event kind, uint32_t &previous, uint32_t value, uint16_t detail = 0) {
    if (previous != value) P3C3::emit(now, kind, detail, value);
    previous = value;
  };
  update(Flight::HeapMinimum, lastHeap, esp_get_minimum_free_heap_size());
  update(Flight::MainStackMinimum, lastStack, uxTaskGetStackHighWaterMark(nullptr));
  update(Flight::WearerStackMinimum, lastWorkerStack, worker.metrics.p3StackMinimum);
  update(Flight::WearerWorkMaximum, lastWorkerWork, worker.metrics.p3WorkMaxUs);
  update(Flight::OledPages, lastOled, worker.metrics.oledPages);
  update(Flight::SensorReads, lastEnergy, worker.metrics.energyReads, 0);
  update(Flight::SensorReads, lastEnvironment, worker.metrics.environmentReads, 1);
  update(Flight::SensorReads, lastCase, worker.metrics.caseReads, 2);
}
#endif
} // namespace
extern "C" void app_main() {
  mainTask = xTaskGetCurrentTaskHandle();
#if PROTOGEN_FLIGHT_RECORDER
  P3C3::emit(P3C3::clockUs(), Flight::Boot, unsigned(esp_reset_reason()), Device::Role);
#endif
  // Do not erase NVS on an error: that would silently lose the existing pair.
  if (!SecureStorage::begin(storage)) {
    puts("Storage unavailable; wireless disabled.");
    return;
  }
  SecureStorage::load(storage, config);
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
#ifdef HELMET_RECEIVER
  gpio_set_direction(static_cast<gpio_num_t>(Device::UnlockPin),
                     GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(Device::UnlockPin),
                     GPIO_PULLUP_ONLY);
  uart_config_t uart{};
  uart.baud_rate = 115200;
  uart.data_bits = UART_DATA_8_BITS;
  uart.parity = UART_PARITY_DISABLE;
  uart.stop_bits = UART_STOP_BITS_1;
  uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart.source_clk = UART_SCLK_DEFAULT;
  if (uart_param_config(UART_NUM_1, &uart) != ESP_OK ||
      uart_set_pin(UART_NUM_1, Device::UartTx, Device::UartRx,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_driver_install(UART_NUM_1, 512, 0, PROTOGEN_MAIN_EVENTS?8:0,
                          PROTOGEN_MAIN_EVENTS?&uartEvents:nullptr, 0) != ESP_OK) {
    puts("UART unavailable; wireless disabled.");
    return;
  }
  if (PROTOGEN_MAIN_EVENTS && xTaskCreate(uartEventTask,"uart_events",2048,nullptr,1,nullptr)!=pdPASS) {
    puts("UART event task unavailable; wireless disabled.");return;
  }
#else
  for (const auto &b : buttons) {
    gpio_set_direction(static_cast<gpio_num_t>(b.pin), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(b.pin), GPIO_PULLUP_ONLY);
  }
#endif
  if (PROTOGEN_MAIN_EVENTS) {
    if (gpio_install_isr_service(0)!=ESP_OK) { puts("GPIO events unavailable; wireless disabled.");return; }
#ifdef HELMET_RECEIVER
    const int eventPins[]={Device::UnlockPin};
#else
    const int eventPins[]={Device::NextButton,Device::PreviousButton,Device::BoopButton};
#endif
    for(const int pin:eventPins) {
      if(gpio_set_intr_type(static_cast<gpio_num_t>(pin),GPIO_INTR_ANYEDGE)!=ESP_OK ||
          gpio_isr_handler_add(static_cast<gpio_num_t>(pin),buttonEdge,nullptr)!=ESP_OK) {
        puts("GPIO event handler unavailable; wireless disabled.");return;
      }
    }
    transport.notifyTask(mainTask);
  }
  gpio_set_direction(static_cast<gpio_num_t>(Device::PairingLed),
                     GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(Device::PairingLed), 0);
  radioStarted = transport.begin() &&
                 wirelessLink.begin(config.paired, config.psk, nowMs());
  WearerIO::start(config);
  printf("{\"event\":\"ready\",\"role\":%u,\"paired\":%s,\"radio\":%s}\n",
         Device::Role, config.paired ? "true" : "false",
         radioStarted ? "true" : "false");
  while (true) {
    const uint64_t workStarted=esp_timer_get_time();
#if PROTOGEN_FLIGHT_RECORDER
    static bool priorValid = false;
    static uint32_t prior = 0;
    const uint32_t started = static_cast<uint32_t>(workStarted);
    if (priorValid) {
      const uint32_t gap = started - prior;
      p3C3Timings[Flight::LoopInterval].observe(gap);
      if (gap > 50000) p3Gap.notify(started, 1, connected ? 1 : 0);
    }
    prior = started;
    priorValid = true;
#endif
    Efficiency::increment(mainExecutions);
    const uint32_t now = nowMs();
#ifdef HELMET_RECEIVER
    serviceUart(now);
#else
    serviceButtons(now);
    static uint32_t batteryAt = 0;
    if (connected && (config.features & 2) && now - batteryAt >= 20000) {
      const auto sensors = WearerIO::snapshot();
      HelmetLink::Packet battery;
      battery.kind = HelmetLink::Status;
      battery.length = 4;
      const float volts = sensors.pawBatteryVolts;
      HelmetLink::put16(battery.payload,
                        volts > 0 && volts <= 6 ? uint16_t(volts * 1000) : 0);
      HelmetLink::put16(battery.payload + 2, sensors.faults);
      if (queueSecure(battery))
        batteryAt = now;
    }
#endif
    serviceUnlock(now);
    serviceConsole(now);
    if (radioStarted) P3_C3_CALL(Radio, wirelessLink.tick(now));
#ifdef HELMET_RECEIVER
    // A command accepted during tick must not wait an extra polling period
    // before beginning UART transmission. This second pass remains bounded.
    if(uartCount)serviceUart(nowMs());
#else
    const unsigned queuedBefore=wirelessLink.queued();
    serviceCommands(nowMs()); // A fresh status received in tick can unblock intent.
    if(wirelessLink.queued()>queuedBefore) P3_C3_CALL(Radio, wirelessLink.tick(nowMs()));
#endif
    uint32_t code = 0;
    const bool hasCode = wirelessLink.pairingCode(code);
    gpio_set_level(
        static_cast<gpio_num_t>(Device::PairingLed),
        indicator.level(now, wirelessLink.pairingActive(), hasCode, code));
    const uint32_t work=static_cast<uint32_t>(esp_timer_get_time()-workStarted);
    mainWorkUs+=work;if(work>mainMaxUs)mainMaxUs=work;
#if PROTOGEN_FLIGHT_RECORDER
    sampleP3(now);
    p3C3Timings[Flight::LoopWork].observe(P3C3::clockUs() - started);
#endif
    if(PROTOGEN_MAIN_EVENTS) {
      const uint32_t end=nowMs();uint32_t wait=wirelessLink.serviceIn(end);
      if(transport.pending())Efficiency::sooner(wait,5);
      // Physical unlock, pairing LEDs and multi-button holds retain 5 ms service.
      if(unlocked || unlockHeld || wirelessLink.pairingActive())Efficiency::sooner(wait,5);
#ifdef HELMET_RECEIVER
      if(uartCount)Efficiency::sooner(wait,5);
      size_t uartBuffered=0;
      if(uart_get_buffered_data_len(UART_NUM_1,&uartBuffered)==ESP_OK && uartBuffered)Efficiency::sooner(wait,5);
      if(connected) {
        Efficiency::sooner(wait,Efficiency::remaining(end,uartHeartbeatAt,Wireless::UartHeartbeatMs));
        if(helmetStatus.available && HelmetLink::u32(helmetStatus.packet.payload)!=helmetStatus.bridge)
          Efficiency::sooner(wait,Efficiency::remaining(end,helloAt,Wireless::ActivePollMs));
      }
#else
      for(const auto &button:buttons) {
        if(button.raw!=button.down)Efficiency::sooner(wait,Efficiency::remaining(end,button.changed,Wireless::DebounceMs));
        if(button.down)Efficiency::sooner(wait,5);
      }
      if(commandWaiting) {
        Efficiency::sooner(wait,Efficiency::remaining(end,commandAt,HelmetLink::FreshMs));
        Efficiency::sooner(wait,Efficiency::remaining(end,retriedAt,Wireless::CommandRetryMs));
      }
      if(actions.pending())Efficiency::sooner(wait,5);
#endif
      const TickType_t ticks=pdMS_TO_TICKS(wait);
      if(ticks)ulTaskNotifyTake(pdTRUE,ticks);
      else {
        // A sub-tick deadline must retain the original yielding poll rather
        // than sleep past it. Pending notifications are still consumed.
        ulTaskNotifyTake(pdTRUE,0);vTaskDelay(0);
      }
    } else vTaskDelay(pdMS_TO_TICKS(Wireless::ServicePollMs));
  }
}
