#include "WearerIO.h"
#include "RetryDelay.h"
#include "SensorMath.h"
#include "Efficiency.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs.h"

namespace WearerIO {
namespace {
Device::Config settings;
Status published;
struct FaceDisplay {
  uint8_t expression, flags;
  uint16_t faults;
  bool operator!=(const FaceDisplay &other) const {
    return expression != other.expression || flags != other.flags || faults != other.faults;
  }
};
FaceDisplay faceStatus{};
uint32_t faceEpoch = 0;
constexpr uint32_t FaceFreshMs = 2000;
bool pawConnected = false;
uint64_t faceAt = 0;
bool faceKnown = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint16_t EnergyMonitorFaultBit = 2;
#ifdef HELMET_RECEIVER
constexpr uint16_t EnvironmentFaultBit = 1;
constexpr uint16_t ScreenFaultBit = 4;
constexpr uint16_t CaseTemperatureFaultBit = 8;
#endif

std::atomic<bool> fullRequested{false};
std::atomic<uint32_t> fullFailureGeneration{0};
std::atomic<TaskHandle_t> worker{nullptr};
void notifyWorker() {
  const auto t = worker.load();
  if (t) xTaskNotifyGive(t);
}
uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
adc_oneshot_unit_handle_t adc = nullptr;
adc_cali_handle_t calibration = nullptr;
bool initAdc() {
  adc_oneshot_unit_init_cfg_t unit{};
  unit.unit_id = ADC_UNIT_1;
  if (!adc && adc_oneshot_new_unit(&unit, &adc) != ESP_OK) return false;
  adc_oneshot_chan_cfg_t channel{};
  channel.bitwidth = ADC_BITWIDTH_DEFAULT;
  channel.atten = ADC_ATTEN_DB_12;
  if (adc_oneshot_config_channel(adc, ADC_CHANNEL_3, &channel) != ESP_OK) return false;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cal{};
  cal.unit_id = ADC_UNIT_1;
  cal.chan = ADC_CHANNEL_3;
  cal.atten = channel.atten;
  cal.bitwidth = channel.bitwidth;
  return adc_cali_create_scheme_curve_fitting(&cal, &calibration) == ESP_OK;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t cal{};
  cal.unit_id = ADC_UNIT_1;
  cal.atten = channel.atten;
  cal.bitwidth = channel.bitwidth;
  return adc_cali_create_scheme_line_fitting(&cal, &calibration) == ESP_OK;
#else
  return false;
#endif
}
// The worker owns the ADC unit and calibration handle. A failed calibration
// keeps its allocated unit and retries without claiming a second ADC resource.
HelmetLink::RetryDelay<5000> adcRetry;
bool adcReady(uint32_t now) {
  if (calibration) return true;
  if (!adcRetry.ready(now)) return false;
  if (initAdc()) return true;
  adcRetry.start(now);
  return false;
}
bool readMv(int &mv) {
  if (!adcReady(nowMs())) return false;
  int sum = 0;
  for (unsigned i = 0; i < 8; ++i) {
    int raw;
    if (adc_oneshot_read(adc, ADC_CHANNEL_3, &raw) != ESP_OK) return false;
    sum += raw;
  }
  return adc_cali_raw_to_voltage(calibration, sum / 8, &mv) == ESP_OK;
}
#ifdef HELMET_RECEIVER
i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t sht = nullptr, ina = nullptr;
spi_device_handle_t oled = nullptr;
bool inaConfigured = false;
EnergyFault inaFault = EnergyFault::None;
uint8_t frame[1024]{}, sentFrame[1024]{};
uint8_t validPages = 0;
bool screenSleeping = false;
bool screenStateKnown = true;
bool screenConfigured = false, screenBusReady = false;
unsigned screenOffset = 0;
bool addDevice(uint8_t address, i2c_master_dev_handle_t &device) {
  i2c_device_config_t d{};
  d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  d.device_address = address;
  d.scl_speed_hz = 100000;
  return i2c_master_bus_add_device(bus, &d, &device) == ESP_OK;
}
bool initBus() {
  i2c_master_bus_config_t b{};
  b.i2c_port = I2C_NUM_0;
  b.sda_io_num = static_cast<gpio_num_t>(Device::I2cSda);
  b.scl_io_num = static_cast<gpio_num_t>(Device::I2cScl);
  b.clk_source = I2C_CLK_SRC_DEFAULT;
  b.glitch_ignore_cnt = 7;
  // External pullups to 3.3 V are mandatory; internal pullups aren't a substitute.
  return i2c_new_master_bus(&b, &bus) == ESP_OK;
}
bool regRead(uint8_t reg, uint8_t *b, size_t n) {
  return ina && i2c_master_transmit_receive(ina, &reg, 1, b, n, 10) == ESP_OK;
}
bool regWrite(uint8_t reg, uint16_t v) {
  const uint8_t b[] = {reg, uint8_t(v >> 8), uint8_t(v)};
  return ina && i2c_master_transmit(ina, b, 3, 10) == ESP_OK;
}
bool configureIna() {
  uint8_t b[2];
  inaFault = EnergyFault::Bus;
  if (!ina && !addDevice(0x40, ina)) return false;
  if (!regRead(0x3e, b, 2)) return false;
  if (SensorMath::bigEndian(b, 2) != 0x5449) {
    inaFault = EnergyFault::Identity;
    return false;
  }
  if (!regRead(0x3f, b, 2)) return false;
  if ((SensorMath::bigEndian(b, 2) & 0xfff0) != 0x2280) {
    inaFault = EnergyFault::Identity;
    return false;
  }
  // 15 mOhm fitted shunt, +/-5 A range, ADCRANGE=0. No accumulator reset.
  inaFault = EnergyFault::Configuration;
  const bool okay =
      regWrite(0x00, 0x0000) && regWrite(0x02, SensorMath::ShuntCalibration) && regWrite(0x01, 0xfb68);
  if (okay) inaFault = EnergyFault::None;
  return okay;
}
bool energy(float &volts, float &amps, uint64_t &counter) {
  uint8_t b[5];
  inaFault = EnergyFault::Bus;
  if (!regRead(0x0b, b, 2)) return false;
  const uint16_t flags = SensorMath::bigEndian(b, 2);
  if (!(flags & 1)) {
    inaFault = EnergyFault::Trim;
    return false;
  }
  if (flags & 0x0a00) {
    inaFault = EnergyFault::Overflow;
    return false;
  }
  // CNVRF proves a completed conversion since DIAG_ALRT was last read.
  // Trim-memory health alone does not make old data fresh.
  if (!(flags & 2)) {
    inaFault = EnergyFault::NotReady;
    return false;
  }
  if (!regRead(0x05, b, 3)) return false;
  const float measuredVolts = (SensorMath::bigEndian(b, 3) >> 4) * 0.0001953125;
  if (!regRead(0x07, b, 3)) return false;
  const float measuredAmps = SensorMath::signedCurrent(b) * SensorMath::CurrentLsb;
  if (!regRead(0x09, b, 5)) return false;
  const uint64_t measuredCounter = SensorMath::bigEndian(b, 5);
  if (!isfinite(measuredVolts) || !isfinite(measuredAmps) || measuredVolts < 9 || measuredVolts > 21 ||
      measuredAmps < -0.01 || measuredAmps > 4.5) {
    inaFault = EnergyFault::Range;
    return false;
  }
  volts = measuredVolts;
  amps = measuredAmps;
  counter = measuredCounter;
  inaFault = EnergyFault::None;
  return true;
}
// Compact original font for the local status panel. Each byte is one 7-pixel column.
const uint8_t Alphabet[26][5]={
 {0x7e,0x09,0x09,0x09,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x41,0x41,0x7f,0x41,0x41},
 {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
 {0x26,0x49,0x49,0x49,0x32},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
 {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}};
const uint8_t Digits[10][5]={
 {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x62,0x51,0x49,0x49,0x46},
 {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3e,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3e}};
void text(unsigned row, const char *s) {
  if (row >= 8) return;
  for (unsigned col = 0; *s && col < 21; ++s, ++col) {
    const unsigned char c = static_cast<unsigned char>(toupper(static_cast<unsigned char>(*s)));
    uint8_t *p = frame + row * 128 + col * 6 + screenOffset;
    if (c >= 'A' && c <= 'Z')
      memcpy(p, Alphabet[c - 'A'], 5);
    else if (c >= '0' && c <= '9')
      memcpy(p, Digits[c - '0'], 5);
    else if (c == '%') {
      p[0] = 0x63;
      p[1] = 0x13;
      p[2] = 0x08;
      p[3] = 0x64;
      p[4] = 0x63;
    } else if (c == ':') {
      p[2] = 0x36;
    } else if (c == '.') {
      p[2] = 0x40;
    } else if (c == '-') {
      memset(p, 8, 5);
    } else if (c == '~') {
      p[0] = 8;
      p[1] = 4;
      p[2] = 8;
      p[3] = 16;
      p[4] = 8;
    } else if (c == '?') {
      p[0] = 2;
      p[1] = 1;
      p[2] = 0x59;
      p[3] = 9;
      p[4] = 6;
    }
  }
}
bool transfer(bool data, const uint8_t *b, size_t n) {
  gpio_set_level(static_cast<gpio_num_t>(Device::ScreenDc), data);
  spi_transaction_t t{};
  t.length = n * 8;
  t.tx_buffer = b;
  return spi_device_polling_transmit(oled, &t) == ESP_OK;
}
bool initScreen() {
  screenConfigured = false;
  screenStateKnown = false;
  validPages = 0;
  gpio_set_direction(static_cast<gpio_num_t>(Device::ScreenDc), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(Device::ScreenReset), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(Device::ScreenReset), 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(Device::ScreenReset), 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  spi_bus_config_t b{};
  b.mosi_io_num = Device::ScreenMosi;
  b.miso_io_num = -1;
  b.sclk_io_num = Device::ScreenSck;
  b.quadwp_io_num = b.quadhd_io_num = -1;
  b.max_transfer_sz = 1024;
  if (!screenBusReady) {
    if (spi_bus_initialize(SPI2_HOST, &b, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    screenBusReady = true;
  }
  spi_device_interface_config_t d{};
  d.clock_speed_hz = 1000000;
  d.mode = 0;
  d.spics_io_num = Device::ScreenCs;
  d.queue_size = 1;
  if (!oled && spi_bus_add_device(SPI2_HOST, &d, &oled) != ESP_OK) return false;
  const uint8_t init[] = {0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0x8d, 0x14, 0x20, 0x00, 0xa1,
                          0xc8, 0xda, 0x12, 0x81, 0x20, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf};
  screenConfigured = transfer(false, init, sizeof(init));
  if (screenConfigured) {
    screenSleeping = false;
    screenStateKnown = true;
  }
  return screenConfigured;
}
const char *warningFor(const Status &s) {
  const char *warning = "STATUS OK";
  if (s.environmentValid && s.temperature >= 35) warning = "HELMET WARM";
  if (s.remainingPercent >= 0 && s.remainingPercent <= 20) warning = "BATTERY LOW";
  if (s.remainingPercent >= 0 && s.remainingPercent <= 10) warning = "BATTERY VERY LOW";
  if (s.environmentValid && s.temperature >= 40) warning = "HELMET HOT";
  if (s.caseValid && s.caseTemperature >= 45) warning = "BATTERY CASE WARM";
  if (s.caseValid && s.caseTemperature >= 50) warning = "BATTERY CASE HOT";
  if (s.faults && !strcmp(warning, "STATUS OK")) warning = "CHECK SENSORS";
  return warning;
}
const char *faceMessage(uint8_t flags, bool fresh) {
  if (!fresh) return "FACE LINK LOST";
  if (flags & HelmetLink::Sleeping) return "FACE ASLEEP";
  if (!(flags & HelmetLink::Visible)) return "FACE HIDDEN";
  if (!(flags & HelmetLink::DisplayReady)) return "FACE UNAVAILABLE";
  if (flags & HelmetLink::Booping) return "BOOP VWV";
  return "FACE READY";
}
// Take one coherent snapshot for both urgent scheduling and the bytes drawn.
// Sample the monotonic clock with the cache so a newly arrived packet cannot
// appear to come from the future, or become fresh again after millis wraps.
struct FaceView {
  FaceDisplay face;
  bool radio;
  uint32_t sampledAt, validForMs;
};
FaceView readFace() {
  portENTER_CRITICAL(&mux);
  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  const uint64_t age = now - faceAt;
  const FaceView view{faceStatus, pawConnected, static_cast<uint32_t>(now),
                      faceKnown && age < FaceFreshMs ? static_cast<uint32_t>(FaceFreshMs - age) : 0};
  portEXIT_CRITICAL(&mux);
  return view;
}
bool showScreen(Status &s, const FaceView &view) {
  const auto &face = view.face;
  const uint32_t now = view.sampledAt;
  const bool fresh = view.validForMs != 0;
  screenOffset = PROTOGEN_OLED_PROTECTION ? (now / 60000) % 2 : 0;
  char line[32];
  memset(frame, 0, sizeof(frame));
  if (!s.energyValid)
    snprintf(line, sizeof(line), "BAT ?  ?V");
  else if (s.remainingPercent >= 0)
    snprintf(line, sizeof(line), "BAT ~%d%% %.1fV", s.remainingPercent, s.volts);
  else
    snprintf(line, sizeof(line), "BAT ?  %.1fV", s.volts);
  text(0, line);
  if (s.environmentValid)
    snprintf(line, sizeof(line), "HEAD %.1fC RH %.0f%%", s.temperature, s.humidity);
  else
    snprintf(line, sizeof(line), "HEAD SENSOR ?");
  text(1, line);
  const char *names[] = {"HAPPY", "SURPRISED", "ANGRY", "VWV", "SLEEPY", "WINK"};
  snprintf(line, sizeof(line), "FACE %s", fresh && face.expression < 6 ? names[face.expression] : "?");
  text(2, line);
  text(3, faceMessage(face.flags, fresh));
  text(4, view.radio ? "PAW CONNECTED" : "PAW OFFLINE");
  if (fresh)
    snprintf(line, sizeof(line), "MIC %s FAULT %u", face.flags & HelmetLink::MicEnabled ? "ON" : "OFF",
             face.faults);
  else
    snprintf(line, sizeof(line), "MIC ? FAULT ?");
  text(5, line);
  if (s.caseValid)
    snprintf(line, sizeof(line), "BAT CASE %.1fC", s.caseTemperature);
  else
    snprintf(line, sizeof(line), "STATUS FAULT %u", s.faults);
  text(6, line);
  const char *warning = warningFor(s);
  text(7, warning);
  if (PROTOGEN_OLED_PROTECTION) {
    const bool sleep = view.radio && fresh && (face.flags & HelmetLink::Sleeping) &&
                       !strcmp(warning, "STATUS OK") && !face.faults;
    if (!screenStateKnown || sleep != screenSleeping) {
      const uint8_t command = sleep ? 0xae : 0xaf;
      if (!transfer(false, &command, 1)) return false;
      screenSleeping = sleep;
      screenStateKnown = true;
      if (!sleep) validPages = 0; // Wake always restores the complete panel image.
    }
    if (sleep) return true;
    const unsigned offset = (now / 60000) % 2;
    const uint8_t commands[] = {0x81, 0x10, 0xd3, static_cast<uint8_t>(offset)};
    if (!transfer(false, commands, sizeof(commands))) return false;
  }
  if (!PROTOGEN_OLED_PAGES) {
    const uint8_t address[] = {0x21, 0, 127, 0x22, 0, 7};
    s.metrics.oledBytes += 1024;
    s.metrics.oledPages += 8;
    return transfer(false, address, sizeof(address)) && transfer(true, frame, sizeof(frame));
  }
  for (unsigned page = 0; page < 8; ++page) {
    const unsigned offset = page * 128;
    if ((validPages & (1u << page)) && !memcmp(frame + offset, sentFrame + offset, 128)) continue;
    const uint8_t address[] = {0x21, 0, 127, 0x22, static_cast<uint8_t>(page), static_cast<uint8_t>(page)};
    s.metrics.oledBytes += 128;
    Efficiency::increment(s.metrics.oledPages);
    if (!transfer(false, address, sizeof(address)) || !transfer(true, frame + offset, 128)) {
      validPages = 0;
      return false; // Retry all pages after any interrupted transfer.
    }
    memcpy(sentFrame + offset, frame + offset, 128);
    validPages |= 1u << page;
  }
  return true;
}
#endif
#if PROTOGEN_FLIGHT_RECORDER
void sampleWorker(Metrics &metrics, uint32_t now, uint32_t elapsed) {
  if (metrics.p3WorkMaxUs < elapsed) metrics.p3WorkMaxUs = elapsed;
  static uint32_t sampledAt = 0;
  if (metrics.p3StackMinimum == UINT32_MAX || now - sampledAt >= 1000) {
    sampledAt = now;
    metrics.p3StackMinimum = uxTaskGetStackHighWaterMark(nullptr);
  }
}
#endif
void task(void *) {
  worker.store(xTaskGetCurrentTaskHandle());
  Status status;
#ifdef HELMET_RECEIVER
  status.energyEnabled = (settings.features & Device::EnergyMonitor) != 0;
  bool energyWasFailed = false;
  const bool environmentEnabled = (settings.features & Device::Environment) != 0;
  const bool usesBus = environmentEnabled || status.energyEnabled;
  HelmetLink::RetryDelay<5000> screenRetry, busRetry, environmentRetry, energyRetry;
  if ((settings.features & Device::Screen) && !initScreen()) {
    status.faults |= ScreenFaultBit;
    screenRetry.start(nowMs());
  }
  if ((settings.features & Device::BatteryCaseTemperature) && !adcReady(nowMs()))
    status.faults |= CaseTemperatureFaultBit;
  nvs_handle_t energyStorage = 0;
  nvs_open("energy", NVS_READWRITE, &energyStorage);
  uint32_t checkpoint = 0;
  if (energyStorage && nvs_get_u32(energyStorage, "used_mwh", &checkpoint) == ESP_OK)
    status.usedMWh = checkpoint;
  // A reset leaves an unmeasured gap. Never present the old checkpoint as known SOC.
  uint32_t environmentRequestAt = 0, energySampleAt = 0, screenAt = 0, caseSampleAt = 0;
  Efficiency::Checkpoint saving;
  saving.known = energyStorage && nvs_get_u32(energyStorage, "used_mwh", &saving.value) == ESP_OK;
  constexpr uint32_t caseIntervalMs = PROTOGEN_CASE_2HZ ? 500 : 20;
  constexpr uint32_t screenIntervalMs = PROTOGEN_OLED_1HZ ? 1000 : 200;
  constexpr uint32_t saveIntervalMs = PROTOGEN_CHECKPOINT_5MIN ? 300000 : 60000;
  FaceDisplay previousFace{};
  bool previousRadio = false, previousStale = true;
  const char *previousWarning = nullptr;
  uint16_t previousFaults = 0xffff;
  uint32_t previousScreenGeneration = UINT32_MAX;
  bool shtPending = false, counterReady = false;
  uint64_t lastCounter = 0;
  while (true) {
    const uint64_t workStarted = esp_timer_get_time();
    const uint32_t now = nowMs();
    Efficiency::increment(status.metrics.executions);
    if ((settings.features & Device::Screen) && !screenConfigured && screenRetry.ready(now)) {
      if (!initScreen()) {
        status.faults |= ScreenFaultBit;
        screenRetry.start(nowMs());
      }
    }
    if (usesBus && !bus && busRetry.ready(now) && !initBus()) {
      if (environmentEnabled) status.faults |= EnvironmentFaultBit;
      if (status.energyEnabled) {
        status.faults |= EnergyMonitorFaultBit;
        status.energyFault = EnergyFault::Bus;
        Efficiency::increment(status.energyFailures);
        energyWasFailed = true;
      }
      busRetry.start(now);
    }
    // Each address has its own retry timer once the controller is available.
    if (bus) {
      if (environmentEnabled && environmentRetry.ready(now)) {
        if (!shtPending && now - environmentRequestAt >= 1000) {
          const uint8_t command = 0xfd;
          environmentRequestAt = now;
          shtPending = (sht || addDevice(0x44, sht)) && i2c_master_transmit(sht, &command, 1, 10) == ESP_OK;
          if (!shtPending) {
            status.environmentValid = false;
            status.faults |= EnvironmentFaultBit;
            environmentRetry.start(now);
          }
        } else if (shtPending && now - environmentRequestAt >= 10) {
          uint8_t b[6];
          shtPending = false;
          Efficiency::increment(status.metrics.environmentReads);
          status.environmentValid = i2c_master_receive(sht, b, 6, 10) == ESP_OK &&
                                    SensorMath::sht40(b, status.temperature, status.humidity);
          if (status.environmentValid)
            status.faults &= static_cast<uint16_t>(~EnvironmentFaultBit);
          else {
            status.faults |= EnvironmentFaultBit;
            environmentRetry.start(now);
          }
        }
      }
      if (status.energyEnabled && energyRetry.ready(now) && now - energySampleAt >= 1000) {
        const uint32_t elapsed = now - energySampleAt;
        energySampleAt = now;
        const bool configuredNow = !inaConfigured && (inaConfigured = configureIna());
        Efficiency::increment(status.metrics.energyReads);
        // Configuring ADC_CONFIG restarts conversions. Keep the existing 1 Hz
        // schedule and read on its next visit, rather than accepting old data.
        if (configuredNow) {
          status.energyValid = false;
          status.energyFault = EnergyFault::NotReady;
        } else {
          float volts = 0, amps = 0;
          uint64_t count = 0;
          status.energyValid = inaConfigured && energy(volts, amps, count);
          if (status.energyValid) {
            bool counterGood = true;
            const bool full = fullRequested.exchange(false) &&
                              fullFailureGeneration.load() == status.energyFailures &&
                              status.energyFailures != UINT32_MAX;
            if (counterReady) {
              const uint64_t delta = (count - lastCounter) & 0xffffffffffULL;
              const double joules = delta * SensorMath::EnergyJoulesLsb;
              if (joules <= 50.0 * elapsed / 1000.0 + 2.0) {
                if (!full) {
                  // The persistent checkpoint is uint32_t mWh. Saturate
                  // before any narrowing; the independent measured total
                  // still accounts for every accepted interval.
                  const double used = status.usedMWh + joules / 3.6;
                  status.usedMWh = used > UINT32_MAX ? double(UINT32_MAX) : used;
                }
                status.measuredMWh += joules / 3.6;
                Efficiency::increment(status.measuredIntervals);
              } else
                counterGood = false;
            }
            lastCounter = count;
            counterReady = true;
            if (counterGood) {
              if (full) {
                status.usedMWh = 0;
                status.estimateQualified = settings.usableEnergyMWh > 0;
              }
              status.volts = volts;
              status.amps = amps;
              status.energyValidAt = now;
              Efficiency::increment(status.energySamples);
              if (energyWasFailed) Efficiency::increment(status.energyRecoveries);
              energyWasFailed = false;
              status.energyFault = EnergyFault::None;
              status.faults &= static_cast<uint16_t>(~EnergyMonitorFaultBit);
            } else {
              // FULL cannot turn an implausible accumulator jump into valid SOC.
              status.energyValid = false;
              status.estimateQualified = false;
              fullRequested.store(false);
              status.energyFault = EnergyFault::Counter;
              status.faults |= EnergyMonitorFaultBit;
              Efficiency::increment(status.energyFailures);
              energyWasFailed = true;
            }
          } else {
            status.energyFault = inaFault;
            status.faults |= EnergyMonitorFaultBit;
            status.estimateQualified = false;
            counterReady = false;
            fullRequested.store(false);
            Efficiency::increment(status.energyFailures);
            energyWasFailed = true;
            if (inaFault != EnergyFault::NotReady) inaConfigured = false;
            energyRetry.start(now);
          }
        }
      }
    }
    if (now - environmentRequestAt > 5000) {
      status.environmentValid = false;
      if (settings.features & Device::Environment) status.faults |= EnvironmentFaultBit;
    }
    if ((settings.features & Device::EnergyMonitor) && now - energySampleAt > 2000) {
      if (status.energyValid) {
        status.energyFault = EnergyFault::Stale;
        Efficiency::increment(status.energyFailures);
        energyWasFailed = true;
      }
      status.energyValid = false;
      status.estimateQualified = false;
      status.faults |= EnergyMonitorFaultBit;
      fullRequested.store(false);
    }
    status.remainingPercent = status.energyValid && status.estimateQualified
                                  ? SensorMath::percent(status.usedMWh, settings.usableEnergyMWh)
                                  : -1;
    if ((settings.features & Device::BatteryCaseTemperature) &&
        ((!PROTOGEN_CASE_2HZ && !PROTOGEN_WEARER_DEADLINES) || now - caseSampleAt >= caseIntervalMs)) {
      caseSampleAt = now;
      Efficiency::increment(status.metrics.caseReads);
      int mv;
      status.caseValid = readMv(mv) && SensorMath::ntc(mv, status.caseTemperature);
      if (status.caseValid) {
        status.faults &= static_cast<uint16_t>(~CaseTemperatureFaultBit);
        if (status.caseTemperature > status.maximumCaseTemperature)
          status.maximumCaseTemperature = status.caseTemperature;
      } else
        status.faults |= CaseTemperatureFaultBit;
    }
    const uint32_t used = static_cast<uint32_t>(status.usedMWh);
    if (energyStorage &&
        (PROTOGEN_CHECKPOINT_5MIN ? saving.due(now, used, status.energyValid, saveIntervalMs)
                                  : (status.energyValid && now - saving.at >= saveIntervalMs))) {
      Efficiency::increment(status.metrics.commitAttempts);
      const bool okay =
          nvs_set_u32(energyStorage, "used_mwh", used) == ESP_OK && nvs_commit(energyStorage) == ESP_OK;
      saving.attempted(now, used, okay);
      Efficiency::increment(okay ? status.metrics.commits : status.metrics.commitFailures);
    }
    const FaceView view = readFace();
    const bool stale = view.validForMs == 0;
    const bool urgent = warningFor(status) != previousWarning || status.faults != previousFaults ||
                        view.radio != previousRadio || stale != previousStale || view.face != previousFace ||
                        (PROTOGEN_OLED_PROTECTION && previousScreenGeneration != view.sampledAt / 60000);
    if (oled && screenConfigured &&
        (now - screenAt >= screenIntervalMs || ((PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION) && urgent))) {
      screenAt = now;
      const uint16_t displayedFaults = status.faults;
      const char *displayedWarning = warningFor(status);
      if (!showScreen(status, view)) {
        status.faults |= ScreenFaultBit;
        validPages = 0;
        screenStateKnown = false;
        screenConfigured = false;
        screenRetry.start(nowMs());
      } else
        status.faults &= static_cast<uint16_t>(~ScreenFaultBit);
      previousFace = view.face;
      previousRadio = view.radio;
      previousStale = stale;
      previousFaults = displayedFaults;
      previousScreenGeneration = view.sampledAt / 60000;
      previousWarning = displayedWarning;
    }
    const uint32_t workUs = static_cast<uint32_t>(esp_timer_get_time() - workStarted);
    status.metrics.workUs += workUs;
    if (workUs > status.metrics.maxWorkUs) status.metrics.maxWorkUs = workUs;
#if PROTOGEN_FLIGHT_RECORDER
    sampleWorker(status.metrics, now, workUs);
#endif
    portENTER_CRITICAL(&mux);
    published = status;
    portEXIT_CRITICAL(&mux);
    if (PROTOGEN_WEARER_DEADLINES) {
      const uint32_t end = nowMs();
      uint32_t waitMs = 1000;
      if (settings.features & Device::BatteryCaseTemperature)
        Efficiency::sooner(waitMs, Efficiency::remaining(end, caseSampleAt, caseIntervalMs));
      if ((settings.features & Device::Screen) && !screenConfigured) {
        Efficiency::sooner(waitMs, screenRetry.remaining(end));
      } else if (oled) {
        Efficiency::sooner(waitMs, Efficiency::remaining(end, screenAt, screenIntervalMs));
        if (!stale) Efficiency::sooner(waitMs, Efficiency::remaining(end, view.sampledAt, view.validForMs));
        if ((PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION) &&
            (status.faults != previousFaults || warningFor(status) != previousWarning))
          Efficiency::sooner(waitMs, 0);
      }
      if (usesBus && !bus)
        Efficiency::sooner(waitMs, busRetry.remaining(end));
      else {
        if (environmentEnabled)
          Efficiency::sooner(waitMs,
                             environmentRetry.ready(end)
                                 ? Efficiency::remaining(end, environmentRequestAt, shtPending ? 10 : 1000)
                                 : environmentRetry.remaining(end));
        if (status.energyEnabled)
          Efficiency::sooner(waitMs, energyRetry.ready(end) ? Efficiency::remaining(end, energySampleAt, 1000)
                                                            : energyRetry.remaining(end));
      }
      const TickType_t ticks = pdMS_TO_TICKS(waitMs);
      if (ticks)
        ulTaskNotifyTake(pdTRUE, ticks);
      else {
        ulTaskNotifyTake(pdTRUE, 0);
        vTaskDelay(0);
      }
    } else
      vTaskDelay(pdMS_TO_TICKS(20));
  }
#else
  if ((settings.features & Device::EnergyMonitor) && !adcReady(nowMs()))
    status.faults |= EnergyMonitorFaultBit;
  while (true) {
#if PROTOGEN_FLIGHT_RECORDER
    const uint64_t p3Started = esp_timer_get_time();
#endif
    Efficiency::increment(status.metrics.executions);
    if (settings.features & Device::EnergyMonitor) {
      Efficiency::increment(status.metrics.energyReads);
      int mv;
      if (readMv(mv) && mv >= 1400 && mv <= 2200) {
        status.pawBatteryVolts = mv / 500.0f;
        status.faults &= static_cast<uint16_t>(~EnergyMonitorFaultBit);
      } else {
        status.pawBatteryVolts = 0;
        status.faults |= EnergyMonitorFaultBit;
      }
    }
#if PROTOGEN_FLIGHT_RECORDER
    sampleWorker(status.metrics, nowMs(), static_cast<uint32_t>(esp_timer_get_time() - p3Started));
#endif
    portENTER_CRITICAL(&mux);
    published = status;
    portEXIT_CRITICAL(&mux);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
#endif
}
}
void start(const Device::Config &config) {
  settings = config;
  if (!settings.features) return;
  if (xTaskCreate(task, "wearer_io", 6144, nullptr, 1, nullptr) != pdPASS) {
    portENTER_CRITICAL(&mux);
    published.faults = 0xffff;
    portEXIT_CRITICAL(&mux);
  }
}
Status snapshot() {
  portENTER_CRITICAL(&mux);
  const Status s = published;
  portEXIT_CRITICAL(&mux);
  return s;
}
void link(bool connected) {
  portENTER_CRITICAL(&mux);
  const bool changed = pawConnected != connected;
  pawConnected = connected;
  portEXIT_CRITICAL(&mux);
  if (changed && (PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION || PROTOGEN_WEARER_DEADLINES)) notifyWorker();
}
void face(const HelmetLink::Packet &s, bool connected) {
  portENTER_CRITICAL(&mux);
  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  const FaceDisplay next{s.payload[4], s.payload[5], HelmetLink::u16(s.payload + 10)};
  const bool changed = !faceKnown || pawConnected != connected || faceEpoch != s.epoch ||
                       now - faceAt >= FaceFreshMs || faceStatus != next;
  faceStatus = next;
  faceEpoch = s.epoch;
  pawConnected = connected;
  faceAt = now;
  faceKnown = true;
  portEXIT_CRITICAL(&mux);
  if (changed && (PROTOGEN_OLED_1HZ || PROTOGEN_OLED_PROTECTION || PROTOGEN_WEARER_DEADLINES)) notifyWorker();
}
bool markFull() {
#ifdef HELMET_RECEIVER
  const auto status = snapshot();
  if ((settings.features & Device::EnergyMonitor) && settings.usableEnergyMWh && status.energyValid &&
      status.energyFailures != UINT32_MAX) {
    // A request accepted from a just-stale published snapshot cannot survive a
    // fault that the worker has observed but has not published yet.
    fullFailureGeneration.store(status.energyFailures);
    fullRequested.store(true);
    if (PROTOGEN_WEARER_DEADLINES) notifyWorker();
    return true;
  }
#endif
  return false;
}
}
