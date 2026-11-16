#include "ConfigStore.h"
#include <iostream>

int main() {
  const RuntimeConfig defaults;
  std::cout << configSchemaVersion << ' ' << ConfigStore::RecordVersion << ' '
            << ConfigStore::SlotBytes << ' ' << ConfigStore::HeaderBytes << ' '
            << ConfigStore::PayloadBytes << ' ' << ConfigStore::ApprovalOffset << ' '
            << ConfigStore::CrcOffset << ' ' << ConfigStore::MarkerOffset << ' '
            << ConfigStore::RecordBytes << ' ' << ConfigStore::LegacyBrightnessApproval << ' '
            << Settings::MaxBrightness << ' ' << defaults.brightnessApproval << '\n';
  for (const auto &field : configFields)
    std::cout << field.name << ' ' << field.minimum << ' ' << field.maximum
              << ' ' << defaults.*(field.member) << '\n';
}
