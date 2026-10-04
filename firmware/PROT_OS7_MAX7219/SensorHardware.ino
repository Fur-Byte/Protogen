// Use separate ADC modules: microphone on ADC0, boop on ADC1. Ordinary
// analogRead(A7) would share ADC0 with the microphone on this Teensy pin map.
void setupSensors() {
#if defined(Microphone) || defined(BoopSensor)
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
  return sensorAdc ? sensorAdc->adc1->analogRead(irPin) : -1;
#else
  return -1;
#endif
}
