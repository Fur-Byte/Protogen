#pragma once
#include "Arduino.h"
// Only graphics primitives used by this sketch; real GFX is checked by target builds.
class Adafruit_GFX {
protected:
  int16_t WIDTH, HEIGHT, _width, _height;
  byte rotation = 0;
public:
  Adafruit_GFX(int16_t w, int16_t h): WIDTH(w), HEIGHT(h), _width(w), _height(h) {}
  virtual void drawPixel(int16_t, int16_t, uint16_t) = 0;
  void write(byte) {}
  void setRotation(byte r) { rotation = r & 3; _width = (r&1) ? HEIGHT : WIDTH; _height = (r&1) ? WIDTH : HEIGHT; }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int j=0; j<h; ++j) for (int i=0; i<w; ++i) drawPixel(x+i,y+j,color);
  }
  void drawBitmap(int16_t x, int16_t y, const byte *data, int16_t w, int16_t h, uint16_t color) {
    for (int j=0; j<h; ++j) for (int i=0; i<w; ++i)
      if (data[j*((w+7)/8)+i/8] & (0x80 >> (i&7))) drawPixel(x+i,y+j,color);
  }
};
