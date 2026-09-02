#pragma once

#include <cstddef>

#ifndef CPPGM_TELEMETRY_ENABLED
#define CPPGM_TELEMETRY_ENABLED 1
#endif

namespace cppgm
{

#if CPPGM_TELEMETRY_ENABLED

// Keep diagnostic builds byte-for-byte compatible with the original counter
// representation.  Production telemetry-off builds substitute the no-op type
// below, so hot paths do not need a runtime statistics branch per update.
typedef std::size_t ObservationCounter;

#else

class ObservationCounter
{
public:
	ObservationCounter(std::size_t = 0) {}
	ObservationCounter& operator=(std::size_t) { return *this; }
	ObservationCounter& operator++() { return *this; }
	void operator++(int) {}
	ObservationCounter& operator+=(std::size_t) { return *this; }
	operator std::size_t() const { return 0; }
};

#endif

}
