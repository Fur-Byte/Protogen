#include "ActionQueue.h"
#include "EspNowTransport.h"
#include "PairingIndicator.h"
#include "SecureStorage.h"
#include "WearerIO.h"
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
bool radioStarted = false, connected = false, haveStatus = false;
uint32_t bridge = 0, statusAt = 0;
#ifdef HELMET_RECEIVER
uint32_t helloAt = 0, uartHeartbeatAt = 0, statusSentAt = 0;
HelmetLink::Packet lastSentStatus{};
#endif
HelmetLink::Packet latestStatus{};
bool queueSecure(const HelmetLink::Packet &p) {
#ifdef HELMET_RECEIVER
  if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes) {
    HelmetLink::Packet extended = p;
    extended.length += 4;
    const auto sensors = WearerIO::snapshot();
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes,
                      sensors.remainingPercent < 0 ? 0xffff
                                                   : sensors.remainingPercent);
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes + 2,
                      sensors.faults);
    return wirelessLink.queue(extended);
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
unsigned uartRead = 0, uartWrite = 0, uartCount = 0;
bool queueUart(const HelmetLink::Packet &p) {
  if (uartCount == 4)
    return false;
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
  uartWrite = (uartWrite + 1) % 4;
  ++uartCount;
  return true;
}
void serviceUart(uint32_t now) {
#ifdef PROTOGEN_UART_AUTH
  if (!uartAuthStarted)
    uartAuthStarted = uartDecoder.begin(0, now);
  if (!uartCount) {
    const size_t n =
        uartDecoder.poll(now, uartTx[uartWrite], sizeof(uartTx[0]));
    if (n) {
      uartLengths[uartWrite] = n;
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
      latestStatus = p;
      statusAt = now;
      haveStatus = true;
      WearerIO::face(p, connected);
      if (connected && HelmetLink::u32(p.payload) == bridge &&
          (now - statusSentAt >= Wireless::ActivePollMs &&
           (memcmp(p.payload + 4, lastSentStatus.payload + 4, 4) ||
            memcmp(p.payload + 10, lastSentStatus.payload + 10, 2)))) {
        if (queueSecure(p)) {
          lastSentStatus = p;
          statusSentAt = now;
        }
      }
    } else if (p.kind == HelmetLink::Ack && p.length == 5 && connected &&
               p.epoch == latestStatus.epoch &&
               HelmetLink::u32(p.payload) == bridge)
      queueSecure(p);
  }
  // Wired Teensy liveness causes no RF traffic.
  if (connected && haveStatus && now - statusAt < Wireless::FreshStatusMs &&
      HelmetLink::u32(latestStatus.payload) == bridge &&
      now - uartHeartbeatAt >= Wireless::UartHeartbeatMs) {
    HelmetLink::Packet p;
    p.kind = HelmetLink::Heartbeat;
    p.length = 4;
    p.epoch = latestStatus.epoch;
    p.ticket = latestStatus.ticket;
    HelmetLink::put32(p.payload, bridge);
    if (queueUart(p))
      uartHeartbeatAt = now;
  }
  if (connected && haveStatus && now - statusAt < Wireless::FreshStatusMs &&
      HelmetLink::u32(latestStatus.payload) != bridge &&
      now - helloAt >= Wireless::ActivePollMs) {
    HelmetLink::Packet p;
    p.kind = HelmetLink::Hello;
    p.length = 4;
    p.epoch = latestStatus.epoch;
    p.ticket = latestStatus.ticket;
    HelmetLink::put32(p.payload, bridge);
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
  if (!actions.pending() || !connected || !haveStatus ||
      now - statusAt >= Wireless::FreshStatusMs)
    return;
  Wireless::Intent intent;
  if (!actions.pop(now, intent))
    return;
  if (intent.action == Wireless::Actions::Next ||
      intent.action == Wireless::Actions::Previous) {
    intent.value = (latestStatus.payload[4] +
                    (intent.action == Wireless::Actions::Next ? 1 : 5)) %
                   6;
    intent.action = HelmetLink::Expression;
  } else if (intent.action == Wireless::Actions::ToggleSleep) {
    intent.action = HelmetLink::Sleep;
    intent.value = !(latestStatus.payload[5] & HelmetLink::Sleeping);
  }
  if (commandSequence == UINT32_MAX) {
    buttonsArmed = false;
    actions.clear();
    return;
  }
  HelmetLink::Packet p;
  p.kind = HelmetLink::Command;
  p.length = 8;
  p.epoch = latestStatus.epoch;
  p.ticket = latestStatus.ticket;
  p.sequence = ++commandSequence;
  HelmetLink::put32(p.payload, bridge);
  p.payload[4] = intent.action;
  p.payload[5] = intent.value;
  HelmetLink::put16(p.payload + 6, intent.duration);
  if (queueSecure(p)) {
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
  uartSent = 0;
  if (online) {
    do {
      Wireless::Crypto::random(&bridge, sizeof(bridge));
    } while (!bridge);
    lastSentStatus = HelmetLink::Packet{};
  }
  WearerIO::face(latestStatus, online);
#else
  commandWaiting = buttonsArmed = false;
  commandSequence = 0;
  actions.clear();
  haveStatus = false;
  bridge = 0;
  WearerIO::face(latestStatus, online);
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
    if (haveStatus && now - statusAt < Wireless::FreshStatusMs &&
        HelmetLink::u32(latestStatus.payload) == bridge)
      queueSecure(latestStatus);
    return;
  }
  if ((p.kind == HelmetLink::Command && p.length == 8) ||
      (p.kind == HelmetLink::Heartbeat && p.length == 4)) {
    if (haveStatus && now - statusAt < Wireless::FreshStatusMs &&
        p.epoch == latestStatus.epoch && HelmetLink::u32(p.payload) == bridge)
      queueUart(p);
  }
#else
  if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes &&
      p.epoch && HelmetLink::u32(p.payload)) {
    const uint32_t next = HelmetLink::u32(p.payload);
    if ((haveStatus && p.epoch != latestStatus.epoch) ||
        (bridge && next != bridge)) {
      commandWaiting = buttonsArmed = false;
      commandSequence = 0;
      actions.clear();
    }
    bridge = next;
    latestStatus = p;
    statusAt = now;
    haveStatus = true;
    WearerIO::face(p, true);
  } else if (p.kind == HelmetLink::Ack && p.length == 5 && commandWaiting &&
             p.epoch == pendingCommand.epoch &&
             p.sequence == pendingCommand.sequence &&
             HelmetLink::u32(p.payload) == bridge) {
    commandWaiting = false;
    haveStatus = false;
    wirelessLink.activity(now);
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
    if (unlocked)
      confirmationArmed = true;
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
  if (unlocked && confirmationArmed && wirelessLink.pairingCode(code) && held &&
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
} // namespace
extern "C" void app_main() {
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
      uart_driver_install(UART_NUM_1, 512, 0, 0, nullptr, 0) != ESP_OK) {
    puts("UART unavailable; wireless disabled.");
    return;
  }
#else
  for (const auto &b : buttons) {
    gpio_set_direction(static_cast<gpio_num_t>(b.pin), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(b.pin), GPIO_PULLUP_ONLY);
  }
#endif
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
    if (radioStarted)
      wirelessLink.tick(now);
    uint32_t code = 0;
    const bool hasCode = wirelessLink.pairingCode(code);
    gpio_set_level(
        static_cast<gpio_num_t>(Device::PairingLed),
        indicator.level(now, wirelessLink.pairingActive(), hasCode, code));
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
