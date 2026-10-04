#pragma once
#include <cassert>
#include <iostream>
#include "Arduino.h"
void setColor(int, int, int);
void updateControls(uint32_t);
void updateBoop(uint32_t, bool);
void resetAnimations(uint32_t);
void drawBaseFace();
void drawMouth(const uint8_t *, const uint8_t *);
void Blink(uint32_t);
void glitch(uint32_t);
void microphone();
void pauseMicrophone();
void fft();
void renderFFTBars(float *, uint16_t);
void drawExpression();
#include "PROT_OS7_MAX7219.ino"
#include "Control.ino"
#include "Blink.ino"
#include "Glitch.ino"
#include "Microphone.ino"
#include "FFT.ino"
#include "WriteExpression.ino"

