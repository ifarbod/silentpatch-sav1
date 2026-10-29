#pragma once

#include <cstdint>
#include <ctime>

namespace ConsoleRandomness
{
	// PS2 implementation of rand()
	inline uint64_t seed_rand_ps2 = std::time(nullptr);
	inline int rand31()
	{
		seed_rand_ps2 = 0x5851F42D4C957F2D * seed_rand_ps2 + 1;
		return ((seed_rand_ps2 >> 32) & 0x7FFFFFFF);
	}

	// PS2 rand, but returning a 16-bit value
	inline int rand16()
	{
		return rand31() & 0xFFFF;
	}

	// PS2 rand, but matching PC's RAND_MAX
	inline int rand15()
	{
		return rand31() & 0x7FFF;
	}
}
