#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RedsandHavokLayout
{
	// Skyrim 1.5.97's hkbCharacterData reflection table places
	// characterPropertyValues at 0x80 and footIkDriverInfo at 0x88.
	// CommonLibSSE-NG 7.0.0 incorrectly declares footIkDriverInfo at 0x80.
	// Read only the pointer we need, without changing the dependency's ABI.
	inline constexpr std::size_t seFootIkDriverInfoOffset = 0x88;

	inline bool HasSEFootIKDriverInfo(const void* characterData)
	{
		if (!characterData) {
			return false;
		}
		std::uintptr_t pointer = 0;
		std::memcpy(&pointer, static_cast<const std::byte*>(characterData) + seFootIkDriverInfoOffset, sizeof(pointer));
		return pointer != 0;
	}
}
