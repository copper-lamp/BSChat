// This must be the only translation unit defining LeviLamina's unified
// allocator operators. The exported marker is checked by the mod loader.
#define LL_MEMORY_OPERATORS
#include "ll/api/memory/MemoryOperators.h"

#if defined(_MSC_VER)
#pragma comment(linker, "/WHOLEARCHIVE:LeviLamina.lib")
#endif
