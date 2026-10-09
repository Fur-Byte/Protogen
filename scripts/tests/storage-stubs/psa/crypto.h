#pragma once
#include <stdint.h>
// Only the ID type is needed to compile storage; no crypto operation is mocked.
using psa_key_id_t = uint32_t;
