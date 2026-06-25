#pragma once

#ifdef TRACY_ENABLE
#include <tracy/tracy/Tracy.hpp>
#else
#define ZoneScopedN(name)
#define ZoneScopedNC(name, color)
#define TracyAlloc(ptr, size)
#define TracyFree(ptr)
#define TracyPlot(name, value);
#endif
