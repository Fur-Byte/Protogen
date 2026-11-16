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
void setSleepingLocal(bool);
void renderFace(uint32_t);
void Blink(uint32_t);
void glitch(uint32_t);
void setupSensors();
int readBoopSensor();
int readDisplayVoltage();
uint32_t sensorFailureCount();
uint32_t sensorRetryCount();
uint32_t sensorRecoveryCount();
uint8_t sensorRecoveryState();
uint8_t sensorLastFailure();
uint32_t microphoneFailureCount();
uint32_t microphoneRetryCount();
uint32_t microphoneRecoveryCount();
uint8_t microphoneRecoveryState();
uint8_t microphoneLastFailure();
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
bool usbHasPendingWork();
bool pawHasPendingWork();
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

// Legacy host probes read or seed the same live state, without shadow copies.
inline auto &sleeping = behavior.sleeping;
inline auto &boopState = behavior.boopState;
inline auto &boopArmed = behavior.boopArmed;
inline auto &boopActive = behavior.boopActive;
inline auto &boopFromRemote = behavior.boopFromRemote;
inline auto &remoteBoopDuration = behavior.remoteBoopDuration;
inline auto &rising = behavior.rising;
inline auto &blinkState = behavior.blinkState;
inline auto &transitionActive = behavior.transitionActive;
inline auto &blinkTime = behavior.blinkTime;
inline auto &blinkSpeed = behavior.blinkSpeed;
inline auto &nextBlinkInterval = behavior.nextBlinkInterval;
inline auto &boopTime = behavior.boopTime;
inline auto &blinkTiming = behavior.blinkTiming;
inline auto &blinkRandomPending = behavior.blinkRandomPending;
inline auto &blinkDoublePending = behavior.blinkDoublePending;
inline auto &blinkRandomCount = behavior.blinkRandomCount;
inline auto &state = behavior.expression;
inline auto &Step = behavior.blinkClosure;
inline auto &counter = behavior.glitchFrame;
