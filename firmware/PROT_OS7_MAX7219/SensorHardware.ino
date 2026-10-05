// Use separate ADC modules: microphone on ADC0, boop on ADC1. Ordinary
// analogRead(A7) would share ADC0 with the microphone on this Teensy pin map.
namespace SlowAdc {
#if defined(BoopSensor) || defined(VisorConnector)
int boop=-1, voltage=-1, pendingPin=-1;
bool failed=false, nextVoltage=false;
uint32_t started=0, lastBoop=0, lastVoltage=0;
#endif
}
void setupSensors() {
#if defined(Microphone) || defined(BoopSensor) || defined(VisorConnector)
  sensorAdc = new ADC();
  if (!sensorAdc) return;
  sensorAdc->adc0->setResolution(10); sensorAdc->adc0->setAveraging(1);
  sensorAdc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED);
  sensorAdc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);
  sensorAdc->adc1->setResolution(10); sensorAdc->adc1->setAveraging(1);
#endif
#ifdef BoopSensor
  pinMode(irPin, INPUT);
#endif
#ifdef Microphone
  pinMode(micPin, INPUT);
  for (uint16_t i = 0; i < samples / 2; ++i)
    hammingWindow[i] = 0.54f - 0.46f * cosf(6.28318530718f * i / (samples - 1));
  updateAudioCoefficients();
  noiseFloor = config.noiseFloor;
#endif
}

int readBoopSensor() {
#ifdef BoopSensor
  return SlowAdc::boop;
#else
  return -1;
#endif
}

int readDisplayVoltage() {
#ifdef VisorConnector
  return SlowAdc::voltage;
#else
  return -1;
#endif
}
void serviceSensorAcquisition(uint32_t now) {
#if defined(BoopSensor) || defined(VisorConnector)
  using namespace SlowAdc;
  if(!sensorAdc || failed)return;
#ifndef VisorConnector
  if(sleeping || !visorVisible)return;
#endif
  // Dock voltage is a safety measurement and must stay fresh through sleep or
  // Hall transitions. Physical boop sampling still pauses with the face.
  if(pendingPin>=0) {
    if(sensorAdc->adc1->isComplete()) {
      const int value=sensorAdc->adc1->readSingle();
      if(value<0 || value>1023){failed=true;sensorFaults|=2;boop=voltage=-1;return;}
      if(pendingPin==21)boop=value;
      if(pendingPin==15)voltage=value;
      pendingPin=-1;
    } else {
      if(now-started>=5){failed=true;sensorFaults|=2;boop=voltage=-1;}
      return;
    }
  }
  int pin=-1;
#ifdef VisorConnector
  if(now-lastVoltage>=5 && nextVoltage){pin=15;lastVoltage=now;nextVoltage=false;}
#endif
#ifdef BoopSensor
  if(pin<0 && !sleeping && visorVisible && connectorReady && now-lastBoop>=Settings::BoopPollMs){pin=irPin;lastBoop=now;nextVoltage=true;}
#endif
#ifdef VisorConnector
  if(pin<0 && now-lastVoltage>=5){pin=15;lastVoltage=now;}
#endif
  if(pin<0)return;
  if(!sensorAdc->adc1->startSingleRead(pin)){failed=true;sensorFaults|=2;boop=voltage=-1;return;}
  pendingPin=pin;started=now;
#else
  (void)now;
#endif
}
