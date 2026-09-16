#include "../source/RedsandHavokLayout.h"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

// Model the pinned CommonLib declaration, including its incorrectly named
// member at 0x80. Native bytes at 0x88 must determine the result instead.
template <class T>
struct Pointer
{
	T* value{};
	T* get() const { return value; }
	T* operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }
};

struct LegacyCharacterData
{
	std::array<std::byte, 0x80> prefix{};
	Pointer<void> footIkDriverInfo;  // actually native characterPropertyValues
	Pointer<void> handIkDriverInfo;  // actually native footIkDriverInfo
	std::array<std::byte, 0x20> suffix{};
};
static_assert(offsetof(LegacyCharacterData, footIkDriverInfo) == 0x80);
static_assert(offsetof(LegacyCharacterData, handIkDriverInfo) == 0x88);

struct Setup { Pointer<LegacyCharacterData> data; };
struct Character { Pointer<Setup> setup; void* footIkDriver{}; };

// Retain both runtime cases so reintroducing the former SE-only conditional
// in the actual build fragment fails the AE regression case below.
namespace REL
{
	struct Module
	{
		static inline bool se = true;
		static bool IsSE() { return se; }
	};
}

bool HookWouldCopyPose(const Character* character)
{
#include "../source/HavokFootIK.inc"
		return true;
	}
	return false;
}

int main() try
{
	static_assert(sizeof(std::uintptr_t) == 8, "Skyrim requires a 64-bit build");
	// Independent fixture offsets from SkyrimSE.exe 1.5.97's Havok reflection
	// records; deliberately do not use the implementation's offset constant.
	std::array<std::byte, 0xB0> characterData{};
	const std::uintptr_t propertyValues = 0x1122334455667788;
	const std::uintptr_t footIKInfo = 0x8877665544332211;
	const std::uintptr_t empty = 0;
	check(!RedsandHavokLayout::HasFootIKDriverInfo(nullptr), "null character data accepted");
	check(!RedsandHavokLayout::HasFootIKDriverInfo(characterData.data()), "empty character has foot IK");
	std::memcpy(characterData.data() + 0x80, &propertyValues, sizeof(propertyValues));
	check(!RedsandHavokLayout::HasFootIKDriverInfo(characterData.data()), "character properties mistaken for foot IK");
	std::memcpy(characterData.data() + 0x88, &footIKInfo, sizeof(footIKInfo));
	check(RedsandHavokLayout::HasFootIKDriverInfo(characterData.data()), "valid foot IK was skipped");
	std::memcpy(characterData.data() + 0x80, &empty, sizeof(empty));
	check(RedsandHavokLayout::HasFootIKDriverInfo(characterData.data()), "foot IK incorrectly depends on character properties");
	std::memcpy(characterData.data() + 0x88, &empty, sizeof(empty));
	check(!RedsandHavokLayout::HasFootIKDriverInfo(characterData.data()), "removed foot IK still detected");

	int marker = 0;
	for (const bool isSE : { true, false }) {
		REL::Module::se = isSE;
		LegacyCharacterData nativeData{};
		Setup setup{ { &nativeData } };
		Character character{ { &setup }, &marker };
		nativeData.footIkDriverInfo.value = &marker;
		check(!HookWouldCopyPose(&character), isSE ? "SE copied stale pose for properties only" : "AE copied stale pose for properties only");
		nativeData.handIkDriverInfo.value = &marker;
		check(HookWouldCopyPose(&character), "hook skipped valid foot IK");
		nativeData.footIkDriverInfo.value = nullptr;
		check(HookWouldCopyPose(&character), "hook depends on character properties");
		character.footIkDriver = nullptr;
		check(!HookWouldCopyPose(&character), "hook copied pose without a foot IK driver");
		character.footIkDriver = &marker;
		setup.data.value = nullptr;
		check(!HookWouldCopyPose(&character), "hook copied pose without character data");
		character.setup.value = nullptr;
		check(!HookWouldCopyPose(&character), "hook copied pose without setup");
	}
	std::cout << "PASS: SE/AE foot IK layout and actual hook fragment, properties-only regression, null setup/data/driver\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
