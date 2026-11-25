/*
Software License Agreement (BSD License)

Copyright (c) 2013, Mark Ruys.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include <stdint.h>

// Pure framebuffer operation; the driver owns allocation and SPI transport.
static inline void protogenDrawPixel(uint8_t *bitmap, const uint8_t *matrixPosition,
                                     const uint8_t *matrixRotation, uint8_t hDisplays,
                                     int16_t width, int16_t height, int16_t logicalWidth,
                                     int16_t logicalHeight, uint8_t rotation,
                                     int16_t x, int16_t y, uint16_t color, uint8_t *dirtyRows) {
  if (!bitmap || x < 0 || y < 0 || x >= logicalWidth || y >= logicalHeight) return;
  int16_t tmp;
  switch (rotation) {
    case 1:
      tmp = x; x = width - 1 - y; y = tmp;
      break;
    case 2:
      x = width - 1 - x; y = height - 1 - y;
      break;
    case 3:
      tmp = x; x = y; y = height - 1 - tmp;
      break;
  }

  const uint8_t display = matrixPosition[(x / 8) + hDisplays * (y / 8)];
  x &= 7;
  y &= 7;
  switch (matrixRotation[display]) {
    case 1:
      tmp = x; x = 7 - y; y = tmp;
      break;
    case 2:
      x = 7 - x; y = 7 - y;
      break;
    case 3:
      tmp = x; x = y; y = 7 - tmp;
      break;
  }

  uint8_t *pixel = &bitmap[(uint16_t)display * 8 + x];
  const uint8_t mask = 1u << y;
  const uint8_t updated = color ? (*pixel | mask) : (*pixel & ~mask);
  if (updated != *pixel) {
    *pixel = updated;
    *dirtyRows |= 1u << x;
  }
}
