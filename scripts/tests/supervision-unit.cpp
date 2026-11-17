#include "Supervision.h"
#include <cassert>

int main() {
  Supervision::Observer s;
  s.observe(0xfffffff0u, true, 0xffffffffu, true, 65535);
  s.observe(0x10u, true, 0, true, 0);
  assert(s.foreground.lateEpisodes == 0 && s.render.lateEpisodes == 0 && s.audio.lateEpisodes == 0);
  s.observe(0x150u, true, 1, true, 1);
  assert(s.foreground.lateEpisodes == 1 && s.foreground.maxGap == 320);
  assert(s.render.lateEpisodes == 1 && !s.render.stale);
  assert(s.audio.lateEpisodes == 1 && !s.audio.stale);

  s.observe(0x151u, false, 1, false, 1);
  s.observe(0xf00u, false, 1, false, 1);
  assert(!s.foreground.required && !s.render.required && !s.audio.required);
  assert(s.foreground.lateEpisodes == 1 && s.render.lateEpisodes == 1 && s.audio.lateEpisodes == 1);
  s.observe(0xf01u, true, 2, false, 1);
  s.observe(0xf11u, true, 3, false, 1);
  assert(!s.audio.required && s.audio.lateEpisodes == 1);

  s.storage(72);
  s.observe(0xf66u, true, 4, true, 0);
  assert(s.render.lateEpisodes == 1 && s.render.maxGap == 320);
  assert(s.audio.required && !s.audio.stale);
  s.observe(0xfab, true, 4, true, 0);
  assert(s.render.stale && s.audio.stale);
  s.observe(0xfb0, true, 5, true, 1);
  assert(!s.render.stale && !s.audio.stale);
  assert(s.render.lateEpisodes == 2 && s.audio.lateEpisodes == 2);

  s.storage(500);
  s.observe(0x11b0, true, 6, true, 2);
  assert(s.longSaves == 1 && s.foreground.lateEpisodes == 2);
}
