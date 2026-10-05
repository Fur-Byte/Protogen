/******************************************************************
 A library for controling a set of 8x8 LEDs with a MAX7219 or
 MAX7221 displays.
 This is a plugin for Adafruit's core graphics library, providing
 basic graphics primitives (points, lines, circles, etc.).
 You need to download and install Adafruit_GFX to use this library.

 Adafruit invests time and resources providing this open
 source code, please support Adafruit and open-source hardware
 by purchasing products from Adafruit!

 Written by Mark Ruys.
 BSD license, check license.txt for more information.
 All text above must be included in any redistribution.
 ******************************************************************/

#include "Max72xxPanel.h"
#include <SPI.h>

namespace {
constexpr byte OP_DIGIT0 = 1;
constexpr byte OP_DIGIT7 = 8;
constexpr byte OP_DECODEMODE = 9;
constexpr byte OP_INTENSITY = 10;
constexpr byte OP_SCANLIMIT = 11;
constexpr byte OP_SHUTDOWN = 12;
constexpr byte OP_DISPLAYTEST = 15;
}

Max72xxPanel::Max72xxPanel(byte csPin, byte hDisplays, byte vDisplays)
    : Adafruit_GFX(hDisplays * 8, vDisplays * 8), SPI_CS(csPin), hDisplays(hDisplays) {
  const uint16_t count = static_cast<uint16_t>(hDisplays) * vDisplays;
  if (!count || count > 255) {
    return; // Tile identifiers are bytes; reject unrepresentable layouts.
  }
  bitmap = static_cast<byte*>(calloc(count * 10, 1));
  if (!bitmap) {
    return;
  }
  displayCount = count;
  bitmapSize = count * 8;
  matrixRotation = bitmap + bitmapSize;
  matrixPosition = matrixRotation + count;
  for (uint16_t display = 0; display < count; ++display) {
    matrixPosition[display] = static_cast<byte>(display);
  }
}

Max72xxPanel::~Max72xxPanel() {
  free(bitmap);
}

bool Max72xxPanel::begin(uint32_t spiFrequency, bool stayShutdown) {
  if (!bitmap) {
    return false;
  }
  spiClock = (spiFrequency > 0 && spiFrequency <= 10000000UL) ? spiFrequency : 4000000UL;
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH); // Teensy digitalWrite on INPUT only changes its pull resistor.
  SPI.begin();
  initialized = true;
  shutdown(true);
  spiTransfer(OP_DISPLAYTEST, 0);
  spiTransfer(OP_SCANLIMIT, 7);
  spiTransfer(OP_DECODEMODE, 0);
  setIntensity(1);
  dirtyRows = 0xff;
  write(); // Load the framebuffer before enabling the LEDs.
  if (!stayShutdown) shutdown(false);
  return true;
}

void Max72xxPanel::setPosition(byte display, byte x, byte y) {
  if (display >= displayCount || x >= hDisplays || y >= HEIGHT / 8) {
    return;
  }
  matrixPosition[x + static_cast<uint16_t>(hDisplays) * y] = display;
}

void Max72xxPanel::setRotation(byte display, byte rotation) {
  if (display < displayCount) {
    matrixRotation[display] = rotation & 3;
  }
}

void Max72xxPanel::setRotation(byte rotation) {
  Adafruit_GFX::setRotation(rotation);
}

void Max72xxPanel::shutdown(boolean status) {
  spiTransfer(OP_SHUTDOWN, status ? 0 : 1);
}

void Max72xxPanel::setIntensity(byte intensity) {
  spiTransfer(OP_INTENSITY, intensity > 15 ? 15 : intensity);
}

void Max72xxPanel::fillScreen(uint16_t color) {
  const byte value = color ? 0xff : 0;
  for (uint16_t i = 0; i < bitmapSize; ++i) {
    if (bitmap[i] != value) {
      bitmap[i] = value;
      dirtyRows |= 1u << (i & 7);
    }
  }
}

void Max72xxPanel::drawPixel(int16_t x, int16_t y, uint16_t color) {
  // Clip signed logical coordinates before rotating or narrowing them. In
  // particular, y=-256 and y=256 must never wrap onto the visible canvas.
  if (!bitmap || x < 0 || y < 0 || x >= _width || y >= _height) {
    return;
  }
  int16_t tmp;
  switch (rotation) {
    case 1:
      tmp = x; x = WIDTH - 1 - y; y = tmp;
      break;
    case 2:
      x = WIDTH - 1 - x; y = HEIGHT - 1 - y;
      break;
    case 3:
      tmp = x; x = y; y = HEIGHT - 1 - tmp;
      break;
  }

  const byte display = matrixPosition[(x / 8) + hDisplays * (y / 8)];
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

  byte &pixel = bitmap[static_cast<uint16_t>(display) * 8 + x];
  const byte mask = 1u << y;
  const byte updated = color ? (pixel | mask) : (pixel & ~mask);
  if (updated != pixel) {
    pixel = updated;
    dirtyRows |= 1u << x;
  }
}

void Max72xxPanel::write() {
  if (!initialized || !dirtyRows) {
    return;
  }
  // OP_DIGIT0 = 1, so the loop condition (row >= OP_DIGIT0) terminates at 1 and
  // never wraps a byte to 255. If OP_DIGIT0 were 0 this would loop forever.
  for (byte row = OP_DIGIT7; row >= OP_DIGIT0; --row) {
    if (dirtyRows & (1u << (row - OP_DIGIT0))) {
      spiTransfer(row);
    }
  }
  dirtyRows = 0;
}

void Max72xxPanel::spiTransfer(byte opcode, byte data) {
  if (!initialized) {
    return;
  }
  // spiClock defaults to 4 MHz for chain timing margin. Each transaction loads every
  // tile, farthest first, so commands and pixels have identical chain order.
  SPI.beginTransaction(SPISettings(spiClock, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_CS, LOW);
  for (uint16_t display = displayCount; display > 0; --display) {
    SPI.transfer(opcode);
    SPI.transfer(opcode >= OP_DIGIT0 && opcode <= OP_DIGIT7
                     ? bitmap[(display - 1) * 8 + opcode - OP_DIGIT0] : data);
  }
  digitalWrite(SPI_CS, HIGH);
  SPI.endTransaction();
}
