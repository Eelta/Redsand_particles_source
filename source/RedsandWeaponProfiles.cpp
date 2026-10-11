#include "RedsandWeaponProfiles.h"

#include "RE/Skyrim.h"

namespace Redsand::Weapons
{
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kHandToHandMelee) == 0);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kOneHandSword) == 1);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kOneHandDagger) == 2);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kOneHandAxe) == 3);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kOneHandMace) == 4);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kTwoHandSword) == 5);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kTwoHandAxe) == 6);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kBow) == 7);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kStaff) == 8);
	static_assert(static_cast<std::uint32_t>(RE::WEAPON_TYPE::kCrossbow) == 9);

	Profile ForWeapon(RE::TESObjectWEAP* weapon)
	{
		if (!weapon || weapon->IsDynamicForm()) {
			return Profile::Default;
		}

		const auto* origin = weapon->GetFile(0);
		if (!origin) {
			return Profile::Default;
		}
		const auto type = static_cast<std::uint32_t>(weapon->GetWeaponType());
		return Resolve(origin->GetFilename(), type,
			type == 6 && weapon->HasKeywordString("WeapTypeWarhammer"));
	}
}
