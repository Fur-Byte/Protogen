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
#include "ResponseTiming.h"
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
ResponseTiming responseTiming;
bool transitionVisiblePending=false;
Face::Pose displayedPose;
uint8_t transitionStartClosure=0;
uint32_t localCommandSequence=0, usbCommandSequence=0;
Face::Frame responseBaseline;
uint32_t blinkRandomStarted=0, blinkFallbacks=0;
bool brightnessBoostActive = false;
uint32_t brightnessBoostAt = 0, brightnessBoostMs = 0;
void cancelBrightnessBoost() {
  if (brightnessBoostActive) matrix.setIntensity(config.effectiveBrightness());
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
  if (boopActive && now - boopTime >= (boopFromRemote ? remoteBoopDuration : config.boopDuration)) {
    boopActive = false; boopFromRemote = false; boopArmed = false; rising = true;
  }
  if (sleeping || !visorVisible || !displayReady || !connectorReady) {
    if (!displayBlanked) {
      matrix.shutdown(true);
      if(responseTiming.pending&&responseTiming.action==ResponseTiming::Visibility&&responseTiming.target&&
          sleeping&&visorVisible&&displayReady&&connectorReady)
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
  else { if (microphoneEnabled) P3_TEENSY_CALL(Audio, microphone()); else pauseMicrophone(); glitch(now); Blink(now); }
  if (now - previousMillis >= Settings::IndicatorMs) setColor(0,0,0);
  // Routine frames use16ms; sample first transition feedback at2ms until a
  // visible frame changes. Unchanged rows produce no redundant SPI traffic.
  if (waking || rising || now - lastFrameTime >= (transitionVisiblePending ? 2u : 16u)) {
    renderFace(now); lastFrameTime = now; rising = false;
  }
  if (waking) {
    matrix.shutdown(false); displayBlanked = false;
    if(responseTiming.pending&&responseTiming.action==ResponseTiming::Visibility&&!responseTiming.target)
      responseTiming.observe(micros(),true,false,true);
    else observeExpressionResponse(displayedPose);
  }
#if PROTOGEN_FLIGHT_RECORDER
  P3Teensy::finishLoop();
#endif
}

// Eye-only comparison excludes spectrum/glitch mouth changes. The counterfactual
// source uses the SAME closure/offset, so simply closing the old face cannot be
// reported as requested-expression onset.
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
void observeExpressionResponse(const Face::Pose &pose) {
  if(!responseTiming.pending||displayBlanked)return;
  if(responseTiming.action==ResponseTiming::Visibility)return;
  const bool boop=responseTiming.action==ResponseTiming::Boop;
  if(boop!=pose.boop||(!boop&&pose.expression!=responseTiming.target&& !transitionActive)) {
    responseTiming.delay();return;
  }
  if(!boop&&state!=responseTiming.target)return;
  Face::Pose source=pose;source.boop=false;source.expression=responseTiming.source;
  Face::Frame old;Face::compose(old,source);
  const bool target=boop||pose.expression==responseTiming.target;
  const bool onset=target&&(responseEyesDiffer(displayedFrame,old)||
    (responseTiming.source==responseTiming.target&&responseEyesPresent(displayedFrame)));
  const bool feedback=responseEyesDiffer(displayedFrame,responseBaseline);
  responseTiming.observe(micros(),feedback,onset,target&&!transitionActive&&!blinkState);
}
void renderFace(uint32_t now) {
  P3_TEENSY_SCOPE(p3Render, Render);
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
      const uint32_t scaledRamp = config.transitionMs * 3UL / 8;
      const uint32_t ramp = scaledRamp < Settings::TransitionFeedbackRampMs
          ? scaledRamp : Settings::TransitionFeedbackRampMs;
      if (!Settings::EarlyExpressionReveal && elapsed < half) pose.expression = transitionFrom;
      // Retarget from the last emitted closure, never from the obsolete logical
      // target. The unmodified first transition retains the original curve.
      const uint32_t closeMs=(ramp*(8-transitionStartClosure)+7)/8;
      if (elapsed < closeMs) pose.closure = static_cast<uint8_t>(transitionStartClosure+elapsed*(8-transitionStartClosure)/closeMs);
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
    Efficiency::increment(renderMetrics.skipped); observeExpressionResponse(pose); return;
  }
  const uint32_t started = micros();
  Efficiency::increment(renderMetrics.compositions);
  Face::Frame composed;
  P3_TEENSY_CALL(Compose, Face::compose(composed, pose));
  const bool changed = !frameValid || memcmp(composed.bytes, displayedFrame.bytes, sizeof(composed.bytes));
  if (changed) Efficiency::increment(renderMetrics.changed);
  for (int y = 0; y < Face::Height; ++y)
    for (int x = 0; x < Face::Width; ++x) {
      const bool value = Face::pixel(composed, x, y);
      if (!frameValid || value != Face::pixel(displayedFrame, x, y)) matrix.drawPixel(x, y, value);
    }
  displayedFrame = composed; displayedPose=pose; frameValid = true; P3_TEENSY_CALL(DisplayWrite, matrix.write());
  observeExpressionResponse(pose);
  if(responseTiming.feedback||!responseTiming.pending) transitionVisiblePending=false;
  if (PROTOGEN_CACHE_FACE) visualCache.remember(pose);
  const uint32_t elapsed = micros() - started;
  renderMetrics.totalUs += elapsed;
  if (elapsed > renderMetrics.maxUs) renderMetrics.maxUs = elapsed;
}

void selectExpressionCommand(uint8_t expression,uint32_t now,ResponseTiming::Origin origin,uint32_t id,uint32_t receivedUs) {
  if(expression>=ExpressionCount)return;
  if(expression==state) {responseTiming.skip(id,origin);return;}
  const uint8_t source=frameValid?displayedPose.expression:state;
  const uint8_t closure=frameValid?displayedPose.closure:Step;
  responseBaseline=displayedFrame;
  responseTiming.start(id,receivedUs,origin,ResponseTiming::Expression,source,expression);
  if(source==expression&&!displayedPose.boop&&!displayBlanked&&responseEyesPresent(displayedFrame)) {
    // Reversing to artwork that is still on screen has zero target-onset delay;
    // the record explicitly distinguishes this from newly revealed pixels.
    responseTiming.targetAlreadyVisible=true;responseTiming.onset=true;
  }
  state=expression;resetAnimations(now);rising=true;
  if(!sleeping&&visorVisible&&!boopActive&&config.transitionMs) {
    transitionActive=true;transitionVisiblePending=true;transitionFrom=source;
    transitionStartClosure=closure;transitionStarted=now;
  } else if(sleeping||!visorVisible||boopActive||!displayReady||!connectorReady) responseTiming.delay();
}
void selectExpression(uint8_t expression, uint32_t now) {
  selectExpressionCommand(expression,now,ResponseTiming::Local,++localCommandSequence,micros());
}

void setSleepingCommand(bool value,ResponseTiming::Origin origin,uint32_t id,uint32_t receivedUs) {
  if(sleeping==value){responseTiming.skip(id,origin);return;}
  responseBaseline=displayedFrame;
  responseTiming.start(id,receivedUs,origin,ResponseTiming::Visibility,state,value);
  sleeping=value;
  if(!visorVisible||!displayReady||!connectorReady)responseTiming.delay();
}
void setSleepingLocal(bool value) {
  setSleepingCommand(value,ResponseTiming::Local,++localCommandSequence,micros());
}

void scheduleBlink() {
  nextBlinkInterval = config.blinkInterval;
  blinkTiming = BlinkTiming::Timing{};
  blinkRandomCount = 0; blinkDoublePending = false;blinkRandomStarted=millis();
  blinkRandomPending = config.naturalBlinks != 0;
}
void serviceBlinkRandomness() {
  if (!blinkRandomPending) return;
  // Entropy.random() can wait internally. Check availability first and perform
  // at most two reads per loop; controls/audio keep running if entropy is late.
  while (blinkRandomCount < 2 && Entropy.available())
    blinkRandomWords[blinkRandomCount++] = Entropy.random();
  if (blinkRandomCount < 2) {
    if(millis()-blinkRandomStarted<100)return;
    // Cosmetic fallback only: fixed bounded blink, no synthetic entropy and no
    // coupling to the authentication/TRNG interfaces.
    blinkRandomPending=false;ResponseTiming::add(blinkFallbacks);return;
  }
  blinkTiming = BlinkTiming::sample(config, blinkRandomWords[0], blinkRandomWords[1]);
  nextBlinkInterval = blinkTiming.pauseMs;
  blinkDoublePending = blinkTiming.doubleBlink;
  blinkRandomPending = false;
}
void resetAnimations(uint32_t now) {
  Step = 0; blinkState = false; counter = 0; transitionActive = false; transitionVisiblePending=false;
  blinkTime = blinkSpeed = glitchDelay = glitchSpeed = now;
  scheduleBlink(); pauseMicrophone();
}
void setColor(int red, int green, int blue) {
  static int previousRed = -1, previousGreen = -1, previousBlue = -1;
  if (red == previousRed && green == previousGreen && blue == previousBlue) return;
  previousRed = red; previousGreen = green; previousBlue = blue;
  analogWrite(redPin, red); analogWrite(greenPin, green); analogWrite(bluePin, blue);
}
