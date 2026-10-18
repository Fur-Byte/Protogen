#include "sketch.h"
#include "Efficiency.h"
#include <limits>

int main() {
  setup();loop();
  config.naturalBlinks=0;config.idleMotion=0;config.transitionMs=0;
  resetAnimations(millis());speakingActive=false;
  frameValid=false;renderFace(millis());
  const auto composed=renderMetrics.compositions;
  for(unsigned i=0;i<100;++i)renderFace(millis());
  assert(renderMetrics.compositions==composed && renderMetrics.skipped>=100);
  // Every cached input, including spectrum bytes, must produce the reference
  // canvas. Exercise combinations and direct FFT-buffer changes while speaking.
  for(unsigned expression=0;expression<6;++expression)
    for(unsigned closure=0;closure<=8;++closure)
      for(unsigned mode=0;mode<2;++mode)
        for(unsigned glitchFrame=0;glitchFrame<3;++glitchFrame)
          for(unsigned aperture=0;aperture<=3;++aperture)
            for(int offset=-1;offset<=1;++offset) {
              Face::Pose p;p.expression=expression;p.closure=closure;p.mouthMode=mode;
              p.glitch=glitchFrame;p.aperture=aperture;p.eyeOffset=offset;
              Face::VisualCache cache;
              assert(!cache.unchanged(p));cache.remember(p);assert(cache.unchanged(p));
              for(bool speaking:{false,true})for(bool boop:{false,true}) {
                p.speaking=speaking;p.boop=boop;
                for(unsigned byte=0;byte<16;++byte) {
                  fftIcon1[byte]^=0x80;
                  const bool same=cache.unchanged(p);
                  Face::Frame expected;Face::compose(expected,p);
                  if(same) {
                    Face::Frame previous;Face::compose(previous,cache.previous);
                    assert(!memcmp(expected.bytes,previous.bytes,sizeof(expected.bytes)));
                  }
                  cache.remember(p);assert(cache.unchanged(p));
                  fftIcon2L[byte]^=0x01;
                  if(speaking && !boop && !mode)assert(!cache.unchanged(p));
                  cache.remember(p);
                }
              }
            }
  state=ExpressionAngry;Step=3;counter=2;speakingActive=true;config.mouthMode=0;
  renderFace(millis());const auto beforeFFT=renderMetrics.compositions;
  fftIcon1[0]^=0xff;renderFace(millis());assert(renderMetrics.compositions==beforeFFT+1);
  frameValid=false;renderFace(millis());assert(renderMetrics.compositions==beforeFFT+2);
  const auto beforeWake=renderMetrics.compositions;
  sleeping=true;loop();sleeping=false;loop();assert(renderMetrics.compositions>beforeWake);
  // Expiring boosts never persist or alter saved brightness, including rollover.
  fakeUs=uint64_t(0xfffffff0UL)*1000;
  char boost[]="BOOST 3 100";handleUsbCommand(boost,millis());
  assert(brightnessBoostActive && SPI.registers[10]==Settings::effectiveBrightness(3));
  advanceTime(101000);loop();assert(!brightnessBoostActive && SPI.registers[10]==Settings::effectiveBrightness(config.brightness));
  char invalid[]="BOOST 16 100";handleUsbCommand(invalid,millis());assert(!brightnessBoostActive);
  Efficiency::Checkpoint c;c.at=0xfffffff0UL;c.value=1;c.known=true;
  assert(!c.due(15,2,true,100));assert(c.due(100,2,true,100));
  assert(!c.due(100,1,true,100));assert(!c.due(100,2,false,100));
  c.attempted(100,2,false);assert(!c.due(101,2,true,100));assert(c.due(200,2,true,100));
  c.attempted(200,2,true);assert(!c.due(300,2,true,100));
  uint32_t saturated=UINT32_MAX;Efficiency::increment(saturated);assert(saturated==UINT32_MAX);
  std::cout<<"PASS: visual cache, FFT changes, forced restoration, boost expiry and checkpoint rollover/failures\n";
}
