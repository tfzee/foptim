#pragma once
#if TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(N)
#define ZoneScopedNC(N, C)
#define TracyPlot(N, S)
#define TracyAlloc(N, S)
#define TracyFree(N)
#endif
