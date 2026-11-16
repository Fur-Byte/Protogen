// Export the actual current renderer and driver output for comparison with the
// archived bitmap data, placements, and rotations decoded independently in Python.
#include "Arduino.h"
#include "SPI.h"
#include <cassert>
#include "Icons.h"
#include "FaceRenderer.h"
#include "src/Max72xxPanel/Max72xxPanel.h"
#include <iostream>
#include <iomanip>

int main() {
  Max72xxPanel matrix(8, 14, 1);
  assert(matrix.begin(Settings::SpiFrequencyHz));
  for (int tile = 0; tile < 14; ++tile) matrix.setRotation(tile, 3);
  for (uint8_t expression = 0; expression < 4; ++expression) {
    Face::Pose pose; pose.expression = expression;
    Face::Frame frame; Face::compose(frame, pose, Face::AudioSnapshot{});
    matrix.fillScreen(0);
    for (int y = 0; y < Face::Height; ++y)
      for (int x = 0; x < Face::Width; ++x)
        matrix.drawPixel(x, y, Face::pixel(frame, x, y));
    matrix.write();
    std::cout << std::hex << std::setfill('0');
    for (uint8_t value : frame.bytes) std::cout << std::setw(2) << static_cast<unsigned>(value);
    std::cout << ' ';
    for (const auto &tile : SPI.display)
      for (uint8_t value : tile) std::cout << std::setw(2) << static_cast<unsigned>(value);
    std::cout << '\n';
  }
}
