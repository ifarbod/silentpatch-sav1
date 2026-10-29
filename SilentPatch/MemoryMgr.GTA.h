#pragma once

#include "Utils/MemoryMgr.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <variant>
#include "Utils/Patterns.h"

namespace Memory
{
	struct PatternAndOffset
	{
		constexpr PatternAndOffset( std::string_view pattern, ptrdiff_t offset = 0 ) noexcept
			: pattern(std::move(pattern)), offset(offset)
		{
		}

		constexpr bool Valid() const noexcept { return !pattern.empty(); }

		std::string_view pattern;
		ptrdiff_t offset;
	};

	using AddrVariant = std::variant<intptr_t, PatternAndOffset>;

	namespace internal
	{
		inline int8_t* GetVer()
		{
			static int8_t	bVer = -1;
			return &bVer;
		}

		inline bool* GetEuropean()
		{
			static bool			bEuropean;
			return &bEuropean;
		}
	}
}

namespace Memory
{
	namespace internal
	{
		inline uintptr_t HandlePattern( const PatternAndOffset& pattern )
		{
			hook::pattern p(pattern.pattern);
			if (p.count_hint(1).size() == 1)
			{
				return reinterpret_cast<uintptr_t>(p.get_first<const void>(pattern.offset));
			}
			return 0u;
		}

		inline bool TryMatch_10()
		{
			if ( *(uint32_t*)DynBaseAddress(0x82457C) == 0x94BF )
			{
				// 1.0 US
				*GetVer() = 0;
				*GetEuropean() = false;
				return true;
			}
			if ( *(uint32_t*)DynBaseAddress(0x8245BC) == 0x94BF )
			{
				// 1.0 EU
				*GetVer() = 0;
				*GetEuropean() = true;
				return true;
			}
			return false;
		}

		inline void InitializeVersions()
		{
			if ( *GetVer() == -1 )
			{
				if ( !TryMatch_10() )
				{
					// Not a 1.0 EXE, nothing is patched
					*GetVer() = INT8_MAX;
				}
			}
		}

		inline void InitializeRegion_10()
		{
			signed char*	bVer = GetVer();

			if ( *bVer == -1 )
			{
				if ( !TryMatch_10() )
				{
		#ifdef assert
					assert(!"AddressByRegion_10 on non-1.0 EXE!");
		#endif
				}
			}
		}

		inline uintptr_t AdjustAddress_10(uintptr_t address10)
		{
			if ( *GetEuropean() )
			{
				if ( address10 >= 0x746720 && address10 < 0x857000 )
				{
					if ( address10 >= 0x7BA940 )
						address10 += 0x40;
					else
						address10 += 0x50;
				}
			}
			return address10;
		}

		inline uintptr_t AddressByVersion(AddrVariant address10)
		{
			InitializeVersions();

			if ( auto pao = std::get_if<PatternAndOffset>(&address10) ) return HandlePattern( *pao );

			const uintptr_t addr = std::make_unsigned_t<intptr_t>(*std::get_if<intptr_t>(&address10));
			if (addr == 0)
			{
				assert(false);
				return 0;
			}

			// Adjust to EU if needed
			return AdjustAddress_10(addr);
		}

		inline uintptr_t AddressByRegion_10(uintptr_t address10)
		{
			InitializeRegion_10();

			// Adjust to EU if needed
			return AdjustAddress_10(address10);
		}
	}
}

template<typename T = uintptr_t>
inline T AddressByVersion(Memory::AddrVariant address10)
{
	return T(Memory::internal::AddressByVersion( std::move(address10) ));
}

template<typename T = uintptr_t>
inline T AddressByRegion_10(uintptr_t address10)
{
	return T(Memory::internal::AddressByRegion_10(address10));
}

namespace Memory
{
	struct VersionInfo
	{
		int8_t version;
		bool european;
	};

	inline VersionInfo GetVersion()
	{
		Memory::internal::InitializeVersions();
		return { *Memory::internal::GetVer(), *Memory::internal::GetEuropean() };
	}
};
