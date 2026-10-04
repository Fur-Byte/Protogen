/*
  Version 7.0   Last update: 12-06-2022
  This code is made for using the MAX7219 led matrices in protogen helmets.
  Current features:
  - Multiple expressions with a single button
  - Visor detection by using a hall sensor
  - Boop detection by using an infrared or linetracking sensor
  - Mouth animation with sound detection by using the max4466 microphone from Adafruit

  Repaired for Teensy 4.0. Setup, dependencies, pinout, and hardware checks are
  documented in ../../docs/README_RECOVERY.md.
  The bitmaps in Icons.h are manually mapped for this helmet and must be kept as-is.
*/

#if !defined(ARDUINO_TEENSY40)
#error "Select Teensy 4.0: this sketch's pinout and ADC settings target that board."
#endif

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "src/Max72xxPanel/Max72xxPanel.h"
#include "Icons.h"
#include "Settings.h"

// Optional hardware features. Uncomment only those connected to your helmet:
// #define VisorSensor  // Digital sensor on pin 3; HIGH blanks the display.
// #define BoopSensor   // Analog boop/IR sensor on A7; thresholds are in Settings.h.
// #define Microphone   // Microphone with 0-3.3 V output on A0.

#ifdef Microphone
#include <arduinoFFT.h>
#endif

constexpr int pinCS = 8; // MAX7219 CS; hardware SPI uses MOSI=11 (DIN) and SCK=13 (CLK).
constexpr int numberOfHorizontalDisplays = 14;
constexpr int numberOfVerticalDisplays = 1;

Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);

// Expressions

enum Expression : uint8_t {
  ExpressionHappy = 0,
  ExpressionSurprised = 1,
  ExpressionAngry = 2,
  ExpressionVwv = 3,
  ExpressionCount = 4
};

struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr RgbColor expressionColors[ExpressionCount] = {
  {0, 8, 0}, // Happy: green
  {0, 0, 8}, // Surprised: blue
  {8, 0, 0}, // Angry: red
  {4, 0, 4}  // vwv: purple
};

// I/O pins

constexpr int ButtonPin = 2;
constexpr int VisorPin = 3;
constexpr int redPin = 5; // These pins are used for the expression indicating rgb LED.
constexpr int greenPin = 9; //" "
constexpr int bluePin = 10; // " "
#ifdef BoopSensor
constexpr int irPin = A7; // Analog input for infrared or 'boop' sensor
#endif
#ifdef Microphone
constexpr int micPin = A0;  // Analog input for microphone
#endif

// Icon positions

constexpr int16_t EyeLeft = 96; // Horizontal positions off the icons on the screens
constexpr int16_t MawLeft = 64;
constexpr int16_t NoseLeft = 56;
constexpr int16_t NoseRight = 48;
constexpr int16_t MawRight = 16;
constexpr int16_t EyeRight = 0;

inline void drawMouth(const uint8_t *left, const uint8_t *right) {
  matrix.fillRect(MawLeft, 0, 32, 8, LOW);
  matrix.fillRect(MawRight, 0, 32, 8, LOW);
  matrix.drawBitmap(MawLeft, 0, left, 32, 8, HIGH);
  matrix.drawBitmap(MawRight, 0, right, 32, 8, HIGH);
}

// Variables

uint32_t glitchDelay = 0;
uint32_t glitchSpeed = 0;
uint32_t blinkTime = 0;
uint32_t blinkSpeed = 0;
uint32_t boopTime = 0;
uint32_t speakingDelay = 0;
uint32_t previousMillis = 0;


bool visorVisible = true;
bool sleeping = false;
bool displayBlanked = false;
bool boopState = false;
bool boopArmed = false;
bool boopActive = false;
bool speakingActive = false;

bool rising = true;
byte state = Settings::StartupExpression;
uint8_t Step = 0;
uint8_t counter = 0;


//Blink variables
constexpr uint32_t blinkDelay = Settings::BlinkIntervalMs;
bool blinkState = false;


#ifdef Microphone
// Smoothing uses the same 4 kHz samples as the FFT.
const uint8_t numReadings = 10;
int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 0;                  // the running total
int average = 0;                // the average
float filterDCComponent = 0.0f;
uint16_t microphoneSampleCount = 0;
uint32_t nextSampleTime = 0;
bool microphoneSampling = false;

//FFT variables
constexpr uint16_t samples = 64; // Must remain a power of two.
constexpr uint32_t samplingFrequencyHz = 4000UL;
constexpr uint32_t samplingPeriodUs = 1000000UL / samplingFrequencyHz;
float vReal[samples];
float vImag[samples];
float hammingWindow[samples / 2];
ArduinoFFT<float> FFT(vReal, vImag, samples, static_cast<float>(samplingFrequencyHz));
static_assert(samples == 64, "The mouth renderer expects a 64-sample FFT.");
#endif

// Setup runs once

void setup() {
  matrix.begin(Settings::SpiFrequencyHz);
  matrix.setIntensity(Settings::Brightness);
  analogReadResolution(10); // Preserve the 0..1023 sensor thresholds.
  // Per-tile rotations match this helmet's wiring; the Icons.h bitmaps are manually mapped.
  matrix.setRotation(0, 3); // Right eye
  matrix.setRotation(1, 3); // Right eye
  matrix.setRotation(2, 3); // Right mouth
  matrix.setRotation(3, 3); // Right mouth
  matrix.setRotation(4, 3); // Right mouth
  matrix.setRotation(5, 3); // Right mouth
  matrix.setRotation(6, 3); // Right nose
  matrix.setRotation(7, 3); // Left nose
  matrix.setRotation(8, 3); // Left mouth
  matrix.setRotation(9, 3); // Left mouth
  matrix.setRotation(10, 3); // Left mouth
  matrix.setRotation(11, 3); // Left mouth
  matrix.setRotation(12, 3); // Left eye
  matrix.setRotation(13, 3); // Left eye
  matrix.fillScreen(LOW);

  pinMode(ButtonPin, INPUT_PULLUP);
#ifdef VisorSensor
  pinMode(VisorPin, INPUT_PULLUP); // Also supports open-drain hall sensors.
  visorVisible = digitalRead(VisorPin) == LOW;
#endif
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
#ifdef BoopSensor
  pinMode(irPin, INPUT);
#endif
#ifdef Microphone
  pinMode(micPin, INPUT);
  // Cache the symmetric Hamming window once instead of computing cosines per frame.
  for (uint16_t i = 0; i < samples / 2; ++i) {
    hammingWindow[i] = 0.54f - 0.46f * cosf(6.28318530718f * i / (samples - 1));
  }
#endif
  resetAnimations(millis());
}

// Main loop

void loop() {
  const uint32_t now = millis();
  updateControls(now); // Always service the wake button, including during sleep.

  if (sleeping || !visorVisible) {
    if (!displayBlanked) {
      matrix.shutdown(true);
      setColor(0, 0, 0);
      resetAnimations(now);
      displayBlanked = true;
      boopActive = false;
      rising = true;
    }
    return; // No ADC reads, FFT work, drawing, or repeated SPI commands while hidden.
  }

  const bool waking = displayBlanked;
  if (waking) {
    resetAnimations(now);
    boopActive = false;
    rising = true;
  }
  updateBoop(now, waking);

  if (boopState && boopArmed) {
    if (!boopActive) {
      resetAnimations(now);
      drawBaseFace();
      matrix.drawBitmap(EyeLeft, 0, vwvL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, vwv, 16, 8, HIGH);
      setColor(16, 4, 0);
      previousMillis = now;
      boopTime = now;
      boopActive = true;
      rising = true; // Restore the selected expression after release or timeout.
    }
    if (now - boopTime >= Settings::BoopDurationMs) {
      boopArmed = false; // A sustained obstruction must release before another boop.
      boopActive = false;
    }
  }
  else {
    boopActive = false;
  }

  if (!boopActive) {
    if (rising) {
      resetAnimations(now);
      drawBaseFace();
      drawExpression();
      if (state < ExpressionCount) {
        setColor(expressionColors[state].r, expressionColors[state].g, expressionColors[state].b);
      }
      previousMillis = now;
    }
    microphone();
    glitch(now);
    Blink(now);
  }
  if (now - previousMillis >= Settings::IndicatorMs) {
    setColor(0, 0, 0);
  }
  matrix.write();
  if (waking) {
    // Replace the retained frame while the panels are still off, then enable.
    matrix.shutdown(false);
    displayBlanked = false;
  }
}

void drawBaseFace() {
  matrix.fillScreen(LOW);
  drawMouth(mawL, maw);
  matrix.drawBitmap(NoseLeft, 0, noseL, 8, 8, HIGH);
  matrix.drawBitmap(NoseRight, 0, nose, 8, 8, HIGH);
}

void resetAnimations(uint32_t now) {
  Step = 0;
  blinkState = false;
  counter = 0;
  blinkTime = blinkSpeed = now;
  glitchDelay = glitchSpeed = now;
  pauseMicrophone();
}

// RGB LED

void setColor(int redValue, int greenValue, int blueValue) {
  // Avoid reprogramming PWM on every loop once the indicator is already off.
  static int previousRed = -1, previousGreen = -1, previousBlue = -1;
  if (redValue == previousRed && greenValue == previousGreen && blueValue == previousBlue) {
    return;
  }
  previousRed = redValue;
  previousGreen = greenValue;
  previousBlue = blueValue;
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}
