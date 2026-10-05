#pragma once
#include <cassert>
#include <iostream>
#include "Arduino.h"
#include "Configuration.h"
void setColor(int, int, int);
void updateControls(uint32_t);
void updateBoop(uint32_t, bool);
void resetAnimations(uint32_t);
void scheduleBlink();
void serviceBlinkRandomness();
void selectExpression(uint8_t, uint32_t);
void renderFace(uint32_t);
void Blink(uint32_t);
void glitch(uint32_t);
void setupSensors();
int readBoopSensor();
int readDisplayVoltage();
void serviceSensorAcquisition(uint32_t);
void setupPawLink();
void servicePawLink(uint32_t);
bool pawIsOnline(uint32_t);
uint32_t pawRejectedPackets();
void setupVisorConnector();
void updateVisorConnector(uint32_t);
void microphone();
void pauseMicrophone();
void updateAudioCoefficients();
bool calibrateMicrophone();
void fft();
void renderFFTBars(float *, uint16_t);
void serviceSerial(uint32_t);
#include "PROT_OS7_MAX7219.ino"
#include "Control.ino"
#include "Blink.ino"
#include "Glitch.ino"
#include "SensorHardware.ino"
#include "Microphone.ino"
#include "FFT.ino"
#include "UsbControl.ino"
#include "PawLink.ino"
#include "VisorConnector.ino"
