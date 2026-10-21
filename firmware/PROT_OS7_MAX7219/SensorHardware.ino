// Separate ADC modules: microphone on ADC0, boop/dock on ADC1. Recovery only
// stops/restarts ADC1; ADC0 and its 4 kHz timer are never reinitialized here.
namespace SlowAdc {
#if defined(BoopSensor) || defined(VisorConnector)
int boop=-1, voltage=-1, pendingPin=-1;
bool failed=false, permanent=false, nextVoltage=false, recovering=false;
uint8_t verified=0, lastFailure=0;
uint32_t started=0, lastBoop=0, lastVoltage=0, boopAt=0, voltageAt=0;
uint32_t faultAt=0, retryDelay=0, failures=0, retries=0, recoveries=0;
constexpr uint8_t required = 0
#ifdef BoopSensor
  | 1
#endif
#ifdef VisorConnector
  | 2
#endif
  ;
void fail(uint32_t now, uint8_t reason, bool isPermanent=false) {
  if (sensorAdc) { sensorAdc->adc1->stopContinuous(); sensorAdc->adc1->resetError(); }
  failed=true;permanent=isPermanent;recovering=true;lastFailure=reason;++failures;
  boop=voltage=pendingPin=-1;verified=0;nextVoltage=false;
  // An invalid IR value cannot retain a held reaction or rearm a new one.
  boopState=false;boopArmed=false;
  sensorFaults|=2;faultAt=now;
  retryDelay=retryDelay ? (retryDelay < Settings::SensorRetryMaxMs / 2 ?
    retryDelay * 2 : Settings::SensorRetryMaxMs) : Settings::SensorRetryInitialMs;
}
#endif
}

uint32_t sensorFailureCount() {
#if defined(BoopSensor) || defined(VisorConnector)
  return SlowAdc::failures;
#else
  return 0;
#endif
}
uint32_t sensorRetryCount() {
#if defined(BoopSensor) || defined(VisorConnector)
  return SlowAdc::retries;
#else
  return 0;
#endif
}
uint32_t sensorRecoveryCount() {
#if defined(BoopSensor) || defined(VisorConnector)
  return SlowAdc::recoveries;
#else
  return 0;
#endif
}
uint8_t sensorRecoveryState() {
#if defined(BoopSensor) || defined(VisorConnector)
  return SlowAdc::permanent ? 2 : SlowAdc::failed ? 1 : 0;
#else
  return 0;
#endif
}
uint8_t sensorLastFailure() {
#if defined(BoopSensor) || defined(VisorConnector)
  return SlowAdc::lastFailure;
#else
  return 0;
#endif
}

void setupSensors() {
#if defined(Microphone) || defined(BoopSensor) || defined(VisorConnector)
  sensorAdc = new ADC();
  if (!sensorAdc) {
#if defined(BoopSensor) || defined(VisorConnector)
    SlowAdc::fail(millis(),1,true); // Missing allocation is permanent; never churn the heap.
#endif
#ifdef Microphone
    sensorFaults|=4;
#endif
    return;
  }
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
  return !SlowAdc::failed && millis()-SlowAdc::boopAt < Settings::SensorFreshnessMs ? SlowAdc::boop : -1;
#else
  return -1;
#endif
}

int readDisplayVoltage() {
#ifdef VisorConnector
  return !SlowAdc::failed && millis()-SlowAdc::voltageAt < Settings::SensorFreshnessMs ? SlowAdc::voltage : -1;
#else
  return -1;
#endif
}
void invalidateDisplayVoltage() {
#ifdef VisorConnector
  SlowAdc::voltage=-1;
  // A conversion begun before the new power cycle cannot qualify that cycle.
  if(SlowAdc::pendingPin==Settings::DockVoltagePin) {
    if(sensorAdc)sensorAdc->adc1->stopContinuous();
    SlowAdc::pendingPin=-1;
  }
#endif
}
void serviceSensorAcquisition(uint32_t now) {
#if defined(BoopSensor) || defined(VisorConnector)
  using namespace SlowAdc;
  if(!sensorAdc || permanent)return;
  if(failed) {
    if(now-faultAt<retryDelay)return;
    // stopContinuous selects the disabled ADC channel without a wait. Avoid
    // recalibrate()/new ADC(): those can wait and would disturb microphone ADC0.
    sensorAdc->adc1->stopContinuous();sensorAdc->adc1->resetError();
    ++retries;failed=false;
  }
#ifdef BoopSensor
  if(sleeping || !visorVisible) {
    // A boop conversion belongs to the visible/awake acquisition period that
    // started it. Cancel before pausing so wake cannot time out or accept it.
    // With a connector fitted, leave any rail conversion running for safety.
    if(pendingPin==irPin) {
      sensorAdc->adc1->stopContinuous();sensorAdc->adc1->resetError();
      pendingPin=-1;
    }
    boop=-1;verified&=static_cast<uint8_t>(~1u);
  }
#endif
#ifndef VisorConnector
  if(sleeping || !visorVisible)return;
#endif
  if(pendingPin>=0) {
    // A latched completion can predate a stalled loop. Expire it before reading
    // the result, and date accepted values by acquisition start, never service.
    if(now-started>=Settings::SensorConversionTimeoutMs){fail(now,2);return;}
    if(sensorAdc->adc1->isComplete()) {
      const int value=sensorAdc->adc1->readSingle();
      if(value<0 || value>1023){fail(now,3);return;}
#ifdef BoopSensor
      if(pendingPin==irPin){boop=value;boopAt=started;verified|=1;}
#endif
#ifdef VisorConnector
      if(pendingPin==Settings::DockVoltagePin){voltage=value;voltageAt=started;verified|=2;}
#endif
      pendingPin=-1;
      if(verified==required) {
        sensorFaults&=static_cast<uint16_t>(~2u);retryDelay=0;
        if(recovering){++recoveries;recovering=false;}
      }
    } else {
      return;
    }
  }
  int pin=-1;
#ifdef VisorConnector
  if(now-lastVoltage>=Settings::BoopPollMs && nextVoltage){pin=Settings::DockVoltagePin;lastVoltage=now;nextVoltage=false;}
#endif
#ifdef BoopSensor
  if(pin<0 && !sleeping && visorVisible && connectorReady && now-lastBoop>=Settings::BoopPollMs){pin=irPin;lastBoop=now;nextVoltage=true;}
#endif
#ifdef VisorConnector
  if(pin<0 && now-lastVoltage>=Settings::BoopPollMs){pin=Settings::DockVoltagePin;lastVoltage=now;}
#endif
  if(pin<0)return;
  if(!sensorAdc->adc1->checkPin(pin)){fail(now,5,true);return;}
  if(!sensorAdc->adc1->startSingleRead(pin)){fail(now,4);return;}
  pendingPin=pin;started=now;
#else
  (void)now;
#endif
}
