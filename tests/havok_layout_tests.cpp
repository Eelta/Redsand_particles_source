#include "../source/RedsandHavokLayout.h"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	static_assert(sizeof(std::uintptr_t) == 8, "Skyrim requires a 64-bit build");
	// Independent fixture offsets from SkyrimSE.exe 1.5.97's Havok reflection
	// records; deliberately do not use the implementation's offset constant.
	std::array<std::byte, 0xB0> characterData{};
	const std::uintptr_t propertyValues = 0x1122334455667788;
	const std::uintptr_t footIKInfo = 0x8877665544332211;
	const std::uintptr_t empty = 0;
	check(!RedsandHavokLayout::HasSEFootIKDriverInfo(nullptr), "null character data accepted");
	check(!RedsandHavokLayout::HasSEFootIKDriverInfo(characterData.data()), "empty character has foot IK");
	std::memcpy(characterData.data() + 0x80, &propertyValues, sizeof(propertyValues));
	check(!RedsandHavokLayout::HasSEFootIKDriverInfo(characterData.data()), "character properties mistaken for foot IK");
	std::memcpy(characterData.data() + 0x88, &footIKInfo, sizeof(footIKInfo));
	check(RedsandHavokLayout::HasSEFootIKDriverInfo(characterData.data()), "valid foot IK was skipped");
	std::memcpy(characterData.data() + 0x80, &empty, sizeof(empty));
	check(RedsandHavokLayout::HasSEFootIKDriverInfo(characterData.data()), "foot IK incorrectly depends on character properties");
	std::memcpy(characterData.data() + 0x88, &empty, sizeof(empty));
	check(!RedsandHavokLayout::HasSEFootIKDriverInfo(characterData.data()), "removed foot IK still detected");
	std::cout << "PASS: SE foot IK pointer layout, independent character properties, null data\n";
}
