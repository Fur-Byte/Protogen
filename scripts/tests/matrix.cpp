#include <cassert>
#include <iostream>
#include <type_traits>
#include "Arduino.h"
#include "SPI.h"
#include "src/Max72xxPanel/Max72xxPanel.h"

// Inject allocation failure only into this test translation unit.
static bool failAllocation = false;
static void *testCalloc(size_t count, size_t size) {
  return failAllocation ? nullptr : std::calloc(count, size);
}
#define calloc testCalloc
#include "src/Max72xxPanel/Max72xxPanel.cpp"
#undef calloc

static_assert(!std::is_copy_constructible<Max72xxPanel>::value, "Driver owns its buffer.");
static_assert(!std::is_copy_assignable<Max72xxPanel>::value, "Driver owns its buffer.");

static void checkLayout(byte horizontal, byte vertical, bool exhaustive) {
  const int count = horizontal * vertical;
  SPI = FakeSPI(count);
  Max72xxPanel panel(8, horizontal, vertical);
  panel.fillScreen(1); panel.write(); panel.shutdown(false); panel.setIntensity(7);
  assert(!SPI.begun && SPI.bytes == 0); // No hardware access before begin().
  panel.fillScreen(0);
  assert(panel.begin());
  assert(SPI.registers[9] == 0 && SPI.registers[11] == 7 && SPI.registers[15] == 0);
  assert(SPI.registers[10] == 1 && !SPI.shutdown);
  panel.setIntensity(255); assert(SPI.registers[10] == 15);

  // Reverse the physical tile order to exercise positioning as well as rotations.
  for (int tile = 0; tile < count; ++tile)
    panel.setPosition(count-1-tile, tile % horizontal, tile / horizontal);
  panel.setPosition(255, 0, 0);
  panel.setPosition(0, horizontal, 0);
  panel.setPosition(0, 0, vertical);
  panel.setRotation(255, 0); // Invalid identifiers must be harmless.

  for (int canvasRotation = 0; canvasRotation < 4; ++canvasRotation) {
    panel.setRotation(canvasRotation);
    const int width = (canvasRotation & 1 ? vertical : horizontal) * 8;
    const int height = (canvasRotation & 1 ? horizontal : vertical) * 8;
    for (int tileRotation = 0; tileRotation < 4; ++tileRotation) {
      for (int tile = 0; tile < count; ++tile)
        panel.setRotation(tile, tileRotation + 4); // Rotation wraps modulo four.
      for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (!exhaustive && x != 0 && x != width/2 && x != width-1) continue;
        if (!exhaustive && y != 0 && y != height/2 && y != height-1) continue;
        panel.fillScreen(0); panel.write();
        const auto bytes = SPI.bytes;
        panel.drawPixel(x, y, 1); panel.write();
        assert(SPI.bytes - bytes == static_cast<unsigned>(count * 2));
        // Independently rotate the logical point using repeated quarter turns.
        int px = x, py = y, currentWidth = width, currentHeight = height;
        for (int turn = 0; turn < canvasRotation; ++turn) {
          const int nextX = currentHeight-1-py;
          py = px; px = nextX;
          std::swap(currentWidth, currentHeight);
        }
        const int tile = count - 1 - (px/8 + horizontal*(py/8));
        px %= 8; py %= 8;
        for (int turn = 0; turn < tileRotation; ++turn) {
          const int nextX = 7-py; py = px; px = nextX;
        }
        FakeSPI::Frame expected(count);
        expected[tile][px] = 1u << py;
        assert(SPI.display == expected);
        const auto unchanged = SPI.bytes;
        panel.drawPixel(x, y, 1); panel.write();
        assert(SPI.bytes == unchanged);
      }
      const auto frame = SPI.display;
      const auto bytes = SPI.bytes;
      for (const int invalid : {-32768, -257, -256, -1, 256, 32767}) {
        if (invalid < 0 || invalid >= width) panel.drawPixel(invalid, 0, 1);
        if (invalid < 0 || invalid >= height) panel.drawPixel(0, invalid, 1);
      }
      panel.drawPixel(width, 0, 1); panel.drawPixel(0, height, 1); panel.write();
      assert(SPI.display == frame && SPI.bytes == bytes);
    }
  }
}

int main() {
  checkLayout(2, 2, true);
  checkLayout(7, 2, true);
  checkLayout(32, 1, false);
  checkLayout(1, 32, false);
  checkLayout(255, 1, false);
  checkLayout(1, 255, false);
  for (const auto shape : {std::pair<int,int>{0,0}, {0,1}, {1,0}, {16,16}, {255,255}}) {
    SPI = FakeSPI();
    Max72xxPanel invalid(8, shape.first, shape.second);
    assert(!invalid.begin());
    invalid.setPosition(0,0,0); invalid.setRotation(0,0);
    invalid.fillScreen(1); invalid.drawPixel(0,0,1); invalid.write();
    invalid.shutdown(false); invalid.setIntensity(15);
    assert(!SPI.begun && SPI.bytes == 0);
  }
  SPI = FakeSPI(); failAllocation = true;
  Max72xxPanel failed(8,14,1);
  assert(!failed.begin());
  failed.fillScreen(1); failed.drawPixel(0,0,1); failed.write();
  failed.setPosition(0,0,0); failed.setRotation(0,3); failed.shutdown(false);
  assert(!SPI.begun && SPI.bytes == 0);
  std::cout << "PASS: driver clipping, canvas/tile rotations, multirow mappings, "
               "32/255-tile addressing, invalid layouts, allocation failure, SPI initialization\n";
}
