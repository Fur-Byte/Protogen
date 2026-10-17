/* Protogen helmet controller: Teensy 4.0, original 14 MAX7219 modules.
   Wiring and the manually mapped artwork in Icons.h are preserved.
   See ../../docs/UPGRADE.md for controls, configuration, and validation. */
#if !defined(ARDUINO_TEENSY40)
#error "Select Teensy 4.0: this sketch's pinout and ADC settings target that board."
#endif
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <EEPROM.h>
#include <Entropy.h> // Bundled with the pinned Teensy core; native i.MX RT1062 TRNG.
#include "src/Max72xxPanel/Max72xxPanel.h"
#include "Icons.h"
#include "Configuration.h"
#include "ConfigStore.h"
#include "FaceRenderer.h"
#include "VisualCache.h"
#include "src/HelmetProtocol/Efficiency.h"
#include "BlinkTiming.h"
#include "ControllerIdle.h"
#include "src/HelmetProtocol/HelmetProtocol.h"

// Optional hardware features. Uncomment only those connected to your helmet:
// #define VisorSensor  // Digital sensor on pin 3; HIGH blanks the display.
// #define BoopSensor   // Analog boop/IR sensor on A7; runtime-calibrated thresholds.
// #define Microphone   // Microphone with 0-3.3 V output on A0.
// #define WirelessPaw  // Paired ESP32 receiver on Serial1 (RX 0 / TX 1).
// #define VisorConnector // Protected dock: enable 6, buffer enable 4, pilot 12, sense A1.
#if defined(Microphone) || defined(BoopSensor) || defined(VisorConnector)
#include <ADC.h> // ADC 9.1 bundled with Teensy 1.62.0.
ADC *sensorAdc = nullptr; // Construct during setup(), after Teensy core initialization.
#endif
#ifdef Microphone
#include <arduinoFFT.h>
#endif

constexpr int pinCS = 8;
constexpr int numberOfHorizontalDisplays = Face::Width / 8, numberOfVerticalDisplays = Face::Height / 8;
static_assert(Face::Width % 8 == 0 && Face::Height % 8 == 0,
              "Face geometry must cover complete MAX7219 tiles.");
constexpr int ButtonPin = 2, VisorPin = 3, redPin = 5, greenPin = 9, bluePin = 10;
#ifdef BoopSensor
constexpr int irPin = A7;
#endif
#ifdef Microphone
constexpr int micPin = A0;
#endif
constexpr int16_t EyeLeft = Face::EyeLeft, EyeRight = Face::EyeRight;
constexpr int16_t MawLeft = Face::MouthLeft, MawRight = Face::MouthRight;
constexpr int16_t NoseLeft = Face::NoseLeft, NoseRight = Face::NoseRight;
Max72xxPanel matrix(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
RuntimeConfig config;
struct RgbColor { uint8_t r, g, b; };
constexpr RgbColor expressionColors[ExpressionCount] = {
  {0,8,0}, {0,0,8}, {8,0,0}, {4,0,4}, {0,4,6}, {4,6,0}
};

bool visorVisible = true, sleeping = false, displayBlanked = false, displayReady = false;
bool boopState = false, boopArmed = false, boopActive = false, speakingActive = false;
bool boopFromRemote = false;
#ifdef Microphone
bool microphoneEnabled = true;
#else
bool microphoneEnabled = false;
#endif
uint16_t remoteBoopDuration = 1200;
bool connectorReady = true;
uint16_t sensorFaults = 0;
bool rising = true, blinkState = false, transitionActive = false;
uint8_t state = Settings::StartupExpression, Step = 0, counter = 0, transitionFrom = 0;
uint32_t blinkTime = 0, blinkSpeed = 0, nextBlinkInterval = Settings::BlinkIntervalMs;
uint32_t glitchDelay = 0, glitchSpeed = 0, boopTime = 0, speakingDelay = 0;
uint32_t previousMillis = 0, transitionStarted = 0, lastFrameTime = 0;
BlinkTiming::Timing blinkTiming;
bool blinkRandomPending = false, blinkDoublePending = false;
uint8_t blinkRandomCount = 0;
uint32_t blinkRandomWords[2] = {};
int boopReading = 1023;
Face::Frame displayedFrame;
bool frameValid = false;
Face::VisualCache visualCache;
struct RenderMetrics {
  uint32_t attempts = 0, compositions = 0, skipped = 0, changed = 0, wakes = 0;
  uint32_t maxUs = 0;
  uint64_t totalUs = 0;
} renderMetrics;
ControllerIdle controllerIdle;
bool brightnessBoostActive = false;
uint32_t brightnessBoostAt = 0, brightnessBoostMs = 0;
void cancelBrightnessBoost() {
  if (brightnessBoostActive) matrix.setIntensity(Settings::effectiveBrightness(config.brightness));
  brightnessBoostActive = false;
}

#ifdef Microphone
constexpr uint16_t samples = 64, audioBufferSize = 512;
constexpr uint32_t samplingFrequencyHz = 4000UL, samplingPeriodUs = 250;
static_assert((audioBufferSize & (audioBufferSize - 1)) == 0, "Audio ring must be a power of two.");
volatile uint16_t audioBuffer[audioBufferSize];
volatile uint16_t audioWrite = 0, audioRead = 0;
volatile uint32_t audioDroppedSamples = 0;
volatile uint16_t audioRailRun = 0;
uint16_t lastAudioWrite = 0;
uint32_t audioProgressAt = 0;
uint32_t handledAudioDrops = 0, audioFrames = 0;
uint16_t microphoneSampleCount = 0;
bool microphoneSampling = false, audioPriming = false, frameActivity = false;
bool audioBiasReady = false, calibratingAudio = false;
uint32_t calibrationSamples = 0;
float calibrationTotal = 0, noiseFloor = 0, smoothedEnvelope = 0, audioGate = 65;
float attackCoefficient = 0.0125f, releaseCoefficient = 0.002083333f;
constexpr uint8_t numReadings = 10;
int readings[numReadings] = {}, readIndex = 0, total = 0, average = 0;
float filterDCComponent = 0;
float vReal[samples], vImag[samples], hammingWindow[samples / 2];
ArduinoFFT<float> FFT(vReal, vImag, samples, static_cast<float>(samplingFrequencyHz));
uint8_t mouthAperture = 0;
#endif

void setup() {
  Serial.begin(115200); // USB is optional; startup never waits for a computer.
  Entropy.Initialize(); // Starts hardware entropy collection without waiting for it.
  loadStoredConfig(config);
  state = static_cast<uint8_t>(config.startupExpression);
  displayReady = matrix.begin(Settings::SpiFrequencyHz, true);
  matrix.shutdown(true);
  matrix.setIntensity(Settings::effectiveBrightness(config.brightness));
  analogReadResolution(10);
  for (uint8_t tile = 0; tile < numberOfHorizontalDisplays * numberOfVerticalDisplays; ++tile) matrix.setRotation(tile, 3);
  matrix.fillScreen(LOW);
  pinMode(ButtonPin, INPUT_PULLUP);
#ifdef VisorSensor
  pinMode(VisorPin, INPUT_PULLUP);
  visorVisible = digitalRead(VisorPin) == LOW;
#endif
  pinMode(redPin, OUTPUT); pinMode(greenPin, OUTPUT); pinMode(bluePin, OUTPUT);
  setupSensors();
  setupVisorConnector();
  setupPawLink();
  resetAnimations(millis());
  displayBlanked = true; // Compose the first complete face before enabling panels.
}

void loop() {
  const uint32_t now = millis();
  if (brightnessBoostActive && now - brightnessBoostAt >= brightnessBoostMs) cancelBrightnessBoost();
  serviceBlinkRandomness();
  serviceSerial(now);
  servicePawLink(now);
  updateControls(now);
  serviceSensorAcquisition(now);
  updateVisorConnector(now);
  // Reaction lifetime continues while hidden or asleep. Visibility never rearms it.
  if (boopActive && now - boopTime >= (boopFromRemote ? remoteBoopDuration : config.boopDuration)) {
    boopActive = false; boopFromRemote = false; boopArmed = false; rising = true;
  }
  if (sleeping || !visorVisible || !displayReady || !connectorReady) {
    if (!displayBlanked) {
      matrix.shutdown(true); setColor(0,0,0); resetAnimations(now);
      displayBlanked = true;
    }
    // Finish queued USB/UART work before waiting. RX/USB interrupts wake the
    // core; a byte arriving between this check and WFI waits at most one tick.
    bool pending = usbHasPendingWork();
#ifdef WirelessPaw
    pending = pending || pawHasPendingWork();
#endif
    if (!pending) controllerIdle.wait();
    return;
  }
  const bool waking = displayBlanked;
  if (waking) { resetAnimations(now); rising = true; frameValid = false; Efficiency::increment(renderMetrics.wakes); }
  updateBoop(now, waking);
  if (boopState && boopArmed && !boopActive) {
    boopActive = true; boopTime = now; previousMillis = now;
    boopFromRemote = false;
    resetAnimations(now); setColor(16,4,0); rising = true;
  }
  if (boopActive && !boopFromRemote && config.boopMode == 0 && !boopState) {
    boopActive = false; resetAnimations(now); rising = true;
  }
  if (rising && !boopActive) {
    setColor(expressionColors[state].r, expressionColors[state].g, expressionColors[state].b);
    previousMillis = now;
  }
  if (boopActive) pauseMicrophone();
  else { if (microphoneEnabled) microphone(); else pauseMicrophone(); glitch(now); Blink(now); }
  if (now - previousMillis >= Settings::IndicatorMs) setColor(0,0,0);
  // At most 60-ish display frames per second; inputs/audio are serviced every loop.
  if (waking || rising || now - lastFrameTime >= 16) {
    renderFace(now); lastFrameTime = now; rising = false;
  }
  if (waking) { matrix.shutdown(false); displayBlanked = false; }
}

void renderFace(uint32_t now) {
  Efficiency::increment(renderMetrics.attempts);
  Face::Pose pose;
  pose.expression = state; pose.boop = boopActive;
  pose.speaking = speakingActive; pose.glitch = counter; pose.closure = Step;
  pose.mouthMode = static_cast<uint8_t>(config.mouthMode);
#ifdef Microphone
  pose.aperture = mouthAperture;
#endif
  if (transitionActive) {
    const uint32_t elapsed = now - transitionStarted;
    if (!config.transitionMs || elapsed >= config.transitionMs) transitionActive = false;
    else {
      const uint32_t half = config.transitionMs / 2;
      const uint32_t ramp = config.transitionMs * 3UL / 8;
      if (elapsed < half) pose.expression = transitionFrom;
      // A short closed interval ensures the swap is hidden even at 60 Hz.
      if (elapsed < ramp) pose.closure = static_cast<uint8_t>(elapsed * 8 / ramp);
      else if (elapsed < config.transitionMs - ramp) pose.closure = 8;
      else pose.closure = static_cast<uint8_t>((config.transitionMs - elapsed) * 8 / (ramp ? ramp : 1));
    }
  }
  if (config.idleMotion && !blinkState && !transitionActive && !boopActive) {
    // Small, infrequent glances. Exact artwork at rest, no invented extra pixels.
    const uint32_t phase = (now - blinkTime) % 6000;
    pose.eyeOffset = phase >= 2200 && phase < 2500 ? 1 : phase >= 4400 && phase < 4700 ? -1 : 0;
  }
  if (PROTOGEN_CACHE_FACE && frameValid && visualCache.unchanged(pose)) {
    Efficiency::increment(renderMetrics.skipped); return;
  }
  const uint32_t started = micros();
  Efficiency::increment(renderMetrics.compositions);
  Face::Frame composed;
  Face::compose(composed, pose);
  if (!frameValid || memcmp(composed.bytes, displayedFrame.bytes, sizeof(composed.bytes)))
    Efficiency::increment(renderMetrics.changed);
  for (int y = 0; y < Face::Height; ++y)
    for (int x = 0; x < Face::Width; ++x) {
      const bool value = Face::pixel(composed, x, y);
      if (!frameValid || value != Face::pixel(displayedFrame, x, y)) matrix.drawPixel(x, y, value);
    }
  displayedFrame = composed; frameValid = true; matrix.write();
  if (PROTOGEN_CACHE_FACE) visualCache.remember(pose);
  const uint32_t elapsed = micros() - started;
  renderMetrics.totalUs += elapsed;
  if (elapsed > renderMetrics.maxUs) renderMetrics.maxUs = elapsed;
}

void selectExpression(uint8_t expression, uint32_t now) {
  if (expression >= ExpressionCount || expression == state) return;
  const uint8_t old = state;
  state = expression; resetAnimations(now); rising = true;
  if (!sleeping && visorVisible && !boopActive && config.transitionMs) {
    transitionActive = true; transitionFrom = old; transitionStarted = now;
  }
}

void scheduleBlink() {
  nextBlinkInterval = config.blinkInterval;
  blinkTiming = BlinkTiming::Timing{};
  blinkRandomCount = 0; blinkDoublePending = false;
  blinkRandomPending = config.naturalBlinks != 0;
}
void serviceBlinkRandomness() {
  if (!blinkRandomPending) return;
  // Entropy.random() can wait internally. Check availability first and perform
  // at most two reads per loop; controls/audio keep running if entropy is late.
  while (blinkRandomCount < 2 && Entropy.available())
    blinkRandomWords[blinkRandomCount++] = Entropy.random();
  if (blinkRandomCount < 2) return;
  blinkTiming = BlinkTiming::sample(config, blinkRandomWords[0], blinkRandomWords[1]);
  nextBlinkInterval = blinkTiming.pauseMs;
  blinkDoublePending = blinkTiming.doubleBlink;
  blinkRandomPending = false;
}
void resetAnimations(uint32_t now) {
  Step = 0; blinkState = false; counter = 0; transitionActive = false;
  blinkTime = blinkSpeed = glitchDelay = glitchSpeed = now;
  scheduleBlink(); pauseMicrophone();
}
void setColor(int red, int green, int blue) {
  static int previousRed = -1, previousGreen = -1, previousBlue = -1;
  if (red == previousRed && green == previousGreen && blue == previousBlue) return;
  previousRed = red; previousGreen = green; previousBlue = blue;
  analogWrite(redPin, red); analogWrite(greenPin, green); analogWrite(bluePin, blue);
}
