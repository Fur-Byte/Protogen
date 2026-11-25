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
#include "AudioRing.h"
#include "ConfigStore.h"
#include "FaceRenderer.h"
#include "src/BehaviorCore/BehaviorCore.h"
#include "VisualCache.h"
#include "src/HelmetProtocol/Efficiency.h"
#include "BlinkTiming.h"
#include "ControllerIdle.h"
#include "ResponseTiming.h"
#include "Supervision.h"
#include "src/AudioV2/AudioV2.h"
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

Behavior::Core behavior;
#if PROTOGEN_ANIMATION_TIMELINES && defined(PROTOGEN_LINK_TIMELINE_ASSET)
#include "TimelineAsset.h"
extern "C" __attribute__((used, noinline)) bool protogenStartLinkedTimeline(uint32_t now) {
  return behavior.startTimeline(ProtogenAsset::clip, sizeof(ProtogenAsset::clip), now);
}
#endif
bool visorVisible = true, displayBlanked = false, displayReady = false;
bool speakingActive = false;
#ifdef Microphone
bool microphoneEnabled = true;
#else
bool microphoneEnabled = false;
#endif
bool connectorReady = true;
uint16_t sensorFaults = 0;
uint32_t speakingDelay = 0;
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
ResponseTiming responseTiming;
Face::Pose displayedPose;
uint32_t localCommandSequence=0, usbCommandSequence=0;
Face::Frame responseBaseline;
#if PROTOGEN_ANIMATION_V2
uint8_t responseSourceLeft = 0, responseSourceRight = 0;
#endif
uint32_t blinkFallbacks=0;
bool brightnessBoostActive = false;
uint32_t brightnessBoostAt = 0, brightnessBoostMs = 0;
void cancelBrightnessBoost() {
  if (brightnessBoostActive) matrix.setIntensity(config.effectiveBrightness());
  brightnessBoostActive = false;
}

#ifdef Microphone
constexpr uint16_t samples = 64, audioBufferSize = ProtogenAudioRingSize;
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
#if PROTOGEN_AUDIO_V2
static_assert(samplingFrequencyHz == AudioV2::SampleRate && samples == AudioV2::WindowSamples,
              "Audio V2 and FFT must share the acquisition cadence.");
AudioV2::Detector audioDetector;
AudioV2::Settings audioSettings() {
  return {config.sensitivity, config.noiseMargin, config.noiseFloor,
          config.attackMs, config.releaseMs, static_cast<uint16_t>(Settings::SpeakingHoldMs)};
}
#endif
#endif

#if PROTOGEN_SUPERVISION_OBSERVE
Supervision::Observer supervision;
uint32_t supervisionRenderEpoch = 0;

void observeProgress(bool visible) {
#ifdef Microphone
  const bool draining = microphoneSampling && microphoneRecoveryState() == 0;
  supervision.observe(millis(), visible, supervisionRenderEpoch, draining, audioRead);
#else
  supervision.observe(millis(), visible, supervisionRenderEpoch, false, 0);
#endif
}
#endif

void setup() {
  Serial.begin(115200); // USB is optional; startup never waits for a computer.
  Entropy.Initialize(); // Starts hardware entropy collection without waiting for it.
  loadStoredConfig(config);
  behavior.expression = static_cast<uint8_t>(config.startupExpression);
  displayReady = matrix.begin(Settings::SpiFrequencyHz, true);
  matrix.shutdown(true);
  matrix.setIntensity(config.effectiveBrightness());
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

void renderFrame(uint32_t now, Face::AudioSnapshot &spectrum);
void observeExpressionResponse(const Face::Pose &pose, const Face::AudioSnapshot &spectrum);

void loop() {
  P3_TEENSY_SCOPE(p3Loop, LoopWork);
#if PROTOGEN_FLIGHT_RECORDER
  P3Teensy::beginLoop();
#endif
  const uint32_t now = millis();
  responseTiming.poll(micros());
  if (brightnessBoostActive && now - brightnessBoostAt >= brightnessBoostMs) cancelBrightnessBoost();
  serviceBlinkRandomness();
  P3_TEENSY_CALL(USB, serviceSerial(now));
  P3_TEENSY_CALL(UART, servicePawLink(now));
  updateControls(now);
  P3_TEENSY_CALL(Sensors, serviceSensorAcquisition(now));
  updateVisorConnector(now);
  // Reaction lifetime continues while hidden or asleep. Visibility never rearms it.
#if PROTOGEN_ANIMATION_TIMELINES
  behavior.serviceTimeline(now);
#endif
  behavior.expireBoop(now, config.boopDuration);
  if (behavior.sleeping || !visorVisible || !displayReady || !connectorReady) {
#if PROTOGEN_ANIMATION_V2
    behavior.advanceMotion(now);
#endif
    if (!displayBlanked) {
      matrix.shutdown(true);
      if(responseTiming.pending&&responseTiming.action==ResponseTiming::Visibility&&responseTiming.target&&
          behavior.sleeping&&visorVisible&&displayReady&&connectorReady)
        responseTiming.observe(micros(),true,false,true);
      setColor(0,0,0); resetAnimations(now);
      displayBlanked = true;
    }
    responseTiming.delay();
    // Finish queued USB/UART work before waiting. RX/USB interrupts wake the
    // core; a byte arriving between this check and WFI waits at most one tick.
    bool pending = usbHasPendingWork();
#ifdef WirelessPaw
    pending = pending || pawHasPendingWork();
#endif
#if PROTOGEN_FLIGHT_RECORDER
    P3Teensy::finishLoop();
    p3Loop.stop();
#endif
#if PROTOGEN_SUPERVISION_OBSERVE
    observeProgress(false);
#endif
    if (!pending) controllerIdle.wait();
    return;
  }
  const bool waking = displayBlanked;
  if (waking) { resetAnimations(now); behavior.rising = true; frameValid = false; Efficiency::increment(renderMetrics.wakes); }
  updateBoop(now, waking);
  if (behavior.wantsLocalBoop()) {
    behavior.beginBoop(now);
    resetAnimations(now); setColor(16,4,0); behavior.rising = true;
  }
  if (behavior.releaseHeldBoop(config.boopMode)) {
    resetAnimations(now); behavior.rising = true;
  }
  if (behavior.rising && !behavior.boopActive) {
    setColor(expressionColors[behavior.expression].r, expressionColors[behavior.expression].g, expressionColors[behavior.expression].b);
    behavior.previousMillis = now;
  }
  if (behavior.boopActive) pauseMicrophone();
  else { if (microphoneEnabled) P3_TEENSY_CALL(Audio, microphone()); else pauseMicrophone(); glitch(now); Blink(now); }
  if (now - behavior.previousMillis >= Settings::IndicatorMs) setColor(0,0,0);
  // Render every 16 ms. While waiting for the first visible transition change,
  // check every 2 ms. Unchanged rows produce no SPI traffic.
  if (behavior.frameDue(now, waking)) {
    Face::AudioSnapshot spectrum;
    renderFrame(now, spectrum);
    behavior.commitFrameTime(now);
    if (waking) {
      matrix.shutdown(false); displayBlanked = false;
      if(responseTiming.pending&&responseTiming.action==ResponseTiming::Visibility&&!responseTiming.target)
        responseTiming.observe(micros(),true,false,true);
      else observeExpressionResponse(displayedPose, spectrum);
    }
  }
#if PROTOGEN_SUPERVISION_OBSERVE
  observeProgress(true);
#endif
#if PROTOGEN_FLIGHT_RECORDER
  P3Teensy::finishLoop();
#endif
}

// Compare only the eyes to exclude speech and glitch effects. Apply the same
// closure and offset to the source artwork so closing the old eyes alone
// cannot count as the onset of the requested expression.
bool responseEyesDiffer(const Face::Frame &a,const Face::Frame &b) {
  for(int y=0;y<Face::Height;++y)for(int x=0;x<16;++x)
    if(Face::pixel(a,x,y)!=Face::pixel(b,x,y)||Face::pixel(a,96+x,y)!=Face::pixel(b,96+x,y))return true;
  return false;
}
bool responseEyesPresent(const Face::Frame &frame) {
  for(int y=0;y<Face::Height;++y)for(int x=0;x<16;++x)
    if(Face::pixel(frame,x,y)||Face::pixel(frame,96+x,y))return true;
  return false;
}
void observeExpressionResponse(const Face::Pose &pose, const Face::AudioSnapshot &spectrum) {
  if(!responseTiming.pending||displayBlanked)return;
  if(responseTiming.action==ResponseTiming::Visibility)return;
  const bool boop=responseTiming.action==ResponseTiming::Boop;
#if PROTOGEN_ANIMATION_V2
  const bool intendedArt = pose.showsExpression(responseTiming.target);
#else
  const bool intendedArt = pose.expression == responseTiming.target;
#endif
  if(boop!=pose.boop||(!boop&&!intendedArt&& !behavior.transitionActive)) {
    responseTiming.delay();return;
  }
  if(!boop&&behavior.expression!=responseTiming.target)return;
  Face::Pose source=pose;source.boop=false;source.expression=responseTiming.source;
#if PROTOGEN_ANIMATION_V2
  if (!boop) {
    source.leftEye.expressionOverride = responseSourceLeft;
    source.rightEye.expressionOverride = responseSourceRight;
  }
#endif
  Face::Frame old;Face::compose(old,source,spectrum);
  const bool target=boop||intendedArt;
  const bool onset=target&&(responseEyesDiffer(displayedFrame,old)||
    (responseTiming.source==responseTiming.target&&responseEyesPresent(displayedFrame)));
  const bool feedback=responseEyesDiffer(displayedFrame,responseBaseline);
  responseTiming.observe(micros(),feedback,onset,target&&!behavior.transitionActive&&!behavior.blinkState);
}
void captureSpectrum(Face::AudioSnapshot &snapshot, bool speaking, uint8_t mode) {
  // FFT and composition both run in the owner context. The ADC ISR only
  // acquires samples, so these four copies represent one observation.
  if (!speaking || mode == 1) return;
  memcpy(snapshot.first, fftIcon1, sizeof(snapshot.first));
  memcpy(snapshot.firstLeft, fftIcon1L, sizeof(snapshot.firstLeft));
  memcpy(snapshot.second, fftIcon2, sizeof(snapshot.second));
  memcpy(snapshot.secondLeft, fftIcon2L, sizeof(snapshot.secondLeft));
}

void renderFrame(uint32_t now, Face::AudioSnapshot &spectrum) {
  P3_TEENSY_SCOPE(p3Render, Render);
  Efficiency::increment(renderMetrics.attempts);
#if PROTOGEN_SUPERVISION_OBSERVE
  ++supervisionRenderEpoch;
#endif
#ifdef Microphone
  const uint8_t aperture = mouthAperture;
#else
  const uint8_t aperture = 0;
#endif
  const auto pose = behavior.poseAt(now, config, speakingActive, aperture, Settings::EarlyExpressionReveal
#if PROTOGEN_ANIMATION_V2
      , responseTiming.pending
#endif
  );
  captureSpectrum(spectrum, pose.speaking, pose.mouthMode);
  if (PROTOGEN_CACHE_FACE && frameValid && visualCache.unchanged(pose, spectrum)) {
    Efficiency::increment(renderMetrics.skipped); observeExpressionResponse(pose, spectrum); return;
  }
  const uint32_t started = micros();
  Efficiency::increment(renderMetrics.compositions);
  Face::Frame composed;
  P3_TEENSY_CALL(Compose, Face::compose(composed, pose, spectrum));
  const bool changed = !frameValid || memcmp(composed.bytes, displayedFrame.bytes, sizeof(composed.bytes));
  if (changed) Efficiency::increment(renderMetrics.changed);
  for (int y = 0; y < Face::Height; ++y)
    for (int x = 0; x < Face::Width; ++x) {
      const bool value = Face::pixel(composed, x, y);
      if (!frameValid || value != Face::pixel(displayedFrame, x, y)) matrix.drawPixel(x, y, value);
    }
  displayedFrame = composed; displayedPose=pose; frameValid = true; P3_TEENSY_CALL(DisplayWrite, matrix.write());
  observeExpressionResponse(pose, spectrum);
  if(responseTiming.feedback||!responseTiming.pending) behavior.transitionVisiblePending=false;
  if (PROTOGEN_CACHE_FACE) visualCache.remember(pose, spectrum);
  const uint32_t elapsed = micros() - started;
  renderMetrics.totalUs += elapsed;
  if (elapsed > renderMetrics.maxUs) renderMetrics.maxUs = elapsed;
}

void renderFace(uint32_t now) {
  Face::AudioSnapshot spectrum;
  renderFrame(now, spectrum);
}

void selectExpressionCommand(uint8_t expression,uint32_t now,ResponseTiming::Origin origin,uint32_t id,uint32_t receivedUs) {
  if(expression>=ExpressionCount)return;
  if(expression==behavior.expression) {responseTiming.skip(id,origin);return;}
  uint8_t source=frameValid?displayedPose.expression:behavior.expression;
  const uint8_t closure=frameValid?displayedPose.closure:behavior.blinkClosure;
#if PROTOGEN_ANIMATION_V2
  const bool emitted = frameValid && !displayedPose.boop;
  responseSourceLeft = emitted ? displayedPose.eyeExpression(displayedPose.leftEye) : source;
  responseSourceRight = emitted ? displayedPose.eyeExpression(displayedPose.rightEye) : source;
  if (responseSourceLeft == responseSourceRight) source = responseSourceLeft;
#endif
  responseBaseline=displayedFrame;
  responseTiming.start(id,receivedUs,origin,ResponseTiming::Expression,source,expression);
  if(source==expression&&!displayedPose.boop&&!displayBlanked&&responseEyesPresent(displayedFrame)
#if PROTOGEN_ANIMATION_V2
      && displayedPose.showsExpression(expression)
#endif
  ) {
    // Reversing to artwork that is still on screen has zero target-onset delay;
    // the record explicitly distinguishes this from newly revealed pixels.
    responseTiming.targetAlreadyVisible=true;responseTiming.onset=true;
  }
  behavior.expression=expression;resetAnimations(now);behavior.rising=true;
#if PROTOGEN_ANIMATION_V2
  behavior.requestSettle(now);
#endif
  if (behavior.canTransition(visorVisible, config.transitionMs)) {
    behavior.beginTransition(now, source, closure
#if PROTOGEN_ANIMATION_V2
        , emitted ? displayedPose.leftEye : Behavior::EyeMotion{},
          emitted ? displayedPose.rightEye : Behavior::EyeMotion{}
#endif
    );
  } else if(behavior.sleeping||!visorVisible||behavior.boopActive||!displayReady||!connectorReady) responseTiming.delay();
}
void selectExpression(uint8_t expression, uint32_t now) {
  selectExpressionCommand(expression,now,ResponseTiming::Local,++localCommandSequence,micros());
}

void setSleepingCommand(bool value,ResponseTiming::Origin origin,uint32_t id,uint32_t receivedUs) {
  if(behavior.sleeping==value){responseTiming.skip(id,origin);return;}
  responseBaseline=displayedFrame;
  responseTiming.start(id,receivedUs,origin,ResponseTiming::Visibility,behavior.expression,value);
  behavior.sleeping=value;
  if(!visorVisible||!displayReady||!connectorReady)responseTiming.delay();
}
void setSleepingLocal(bool value) {
  setSleepingCommand(value,ResponseTiming::Local,++localCommandSequence,micros());
}

void scheduleBlink() {
  behavior.scheduleBlink(config, millis());
}
void serviceBlinkRandomness() {
  if (!behavior.blinkRandomPending) return;
  // Hardware entropy remains in the shell. It is read only when available,
  // with the same two-word budget and no effect on the authentication path.
  while (behavior.blinkRandomCount < 2 && Entropy.available())
    behavior.acceptBlinkWord(Entropy.random());
  if (behavior.blinkRandomCount < 2 && millis() - behavior.blinkRandomStarted < 100) return;
  if (!behavior.finishBlinkEntropy(config)) ResponseTiming::add(blinkFallbacks);
}
void resetAnimations(uint32_t now) {
  behavior.resetAnimationAt(now);
  scheduleBlink(); pauseMicrophone();
}
void setColor(int red, int green, int blue) {
  static int previousRed = -1, previousGreen = -1, previousBlue = -1;
  if (red == previousRed && green == previousGreen && blue == previousBlue) return;
  previousRed = red; previousGreen = green; previousBlue = blue;
  analogWrite(redPin, red); analogWrite(greenPin, green); analogWrite(bluePin, blue);
}
