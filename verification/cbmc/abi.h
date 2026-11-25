#pragma once
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
_Static_assert(CHAR_BIT == 8 && sizeof(int) == 4, "32-bit target integer model required");
_Static_assert(sizeof(long) == 4 && sizeof(void *) == 4 && sizeof(size_t) == 4,
               "ILP32 target-width model required");
_Static_assert(sizeof(uint64_t) == 8, "64-bit sequence model required");
