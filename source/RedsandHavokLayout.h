#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RedsandHavokLayout
{
	// hkbCharacterData::footIkDriverInfo is at 0x88 for both SE and AE.
	// Verified against SE 1.5.97 reflection and the shipped Precision 2.0.6
	// DLL's shared SE/AE hit-physics code; see docs/foot-ik-fix.md.
	// The pinned CommonLib revision incorrectly declares it at 0x80, which
	// actually holds characterPropertyValues. Do not fall back to that member
	// on AE. Read only this pointer without changing the dependency's ABI.
	inline constexpr std::size_t footIkDriverInfoOffset = 0x88;

	inline bool HasFootIKDriverInfo(const void* characterData)
	{
		if (!characterData) {
			return false;
		}
		std::uintptr_t pointer = 0;
		std::memcpy(&pointer, static_cast<const std::byte*>(characterData) + footIkDriverInfoOffset, sizeof(pointer));
		return pointer != 0;
	}
}
