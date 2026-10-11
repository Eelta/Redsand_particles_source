#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RedsandHavokLayout
{

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
