#include "../source/RedsandWeaponProfiles.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace Redsand::Weapons;

	void Check(bool condition, const char* message)
	{
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	void EveryVanillaWeaponCategoryHasItsOwnProfile()
	{
		constexpr std::array<Profile, 10> expected = {
			Profile::Default, Profile::Sword, Profile::Dagger, Profile::WarAxe,
			Profile::Mace, Profile::Greatsword, Profile::Battleaxe, Profile::Bow,
			Profile::Default, Profile::Crossbow
		};
		for (const auto plugin : { "Skyrim.esm", "Dawnguard.esm", "Dragonborn.esm" }) {
			for (std::uint32_t type = 0; type < expected.size(); ++type) {
				Check(Resolve(plugin, type) == expected[type], "a vanilla weapon type selected the wrong profile");
			}
			Check(Resolve(plugin, 6, true) == Profile::Warhammer, "warhammer shares the battleaxe animation type but needs its own profile");
			Check(Resolve(plugin, 1, true) == Profile::Sword, "warhammer keyword must only distinguish weapon type six");
			Check(Resolve(plugin, 10) == Profile::Default, "unknown vanilla weapon type needs the default");
			Check(Resolve(plugin, std::numeric_limits<std::uint32_t>::max()) == Profile::Default, "invalid vanilla weapon type needs the default");
		}
	}

	void OriginMatchingCannotAdoptModWeapons()
	{
		Check(Resolve("sKyRiM.EsM", 1) == Profile::Sword, "plugin filename matching should ignore ASCII case");
		Check(Resolve("DAWNGUARD.ESM", 9) == Profile::Crossbow, "mixed-case DLC filename was not accepted");
		Check(Resolve("dragonborn.esm", 9) == Profile::Crossbow, "lowercase DLC filename was not accepted");
		Check(Resolve("Skyrim.esm", 8) == Profile::Default, "staff must not select a removed category effect");
		for (const auto plugin : { "", "MyWeapons.esp", "MyWeapons.esm", "Skyrim.esl", "Skyrim.esm.esp", "MySkyrim.esm", "Skyrim.esm ", "SkyrimVR.esm", "Update.esm", "ccBGSSSE001-Fish.esm", "BowRapidCombo.esp" }) {
			for (std::uint32_t type = 0; type <= 10; ++type) {
				Check(Resolve(plugin, type, true) == Profile::Default, "a mod-created weapon or missing origin acquired a vanilla profile");
			}
		}
	}

	void LocalizationAndLoadOrderCannotChangeAProfile()
	{
		struct RecordFixture
		{
			std::string_view name;
			std::uint32_t formID;
			Descriptor descriptor;
		};

		constexpr std::array<RecordFixture, 4> records = {{
			{ "Iron Sword", 0x00012EB7, { "Skyrim.esm", 1 } },
			{ "\xE9\x93\x81\xE5\x89\x91", 0x00012EB7, { "Skyrim.esm", 1 } },
			{ "Dragonborn weapon", 0x02012345, { "Dragonborn.esm", 1 } },
			{ "Localized DLC weapon", 0x7A012345, { "Dragonborn.esm", 1 } }
		}};
		for (const auto& record : records) {
			Check(Resolve(record.descriptor) == Profile::Sword, "localization or DLC load order changed the weapon profile");
		}
		Check(Resolve({ "SwordCopy.esp", 1, false }) == Profile::Default, "a mod copying a vanilla weapon must keep the default");
	}

	void EveryReleasedColorMapsWithoutLosingEnchantmentColor()
	{
		constexpr std::array profiles = {
			Profile::Sword, Profile::Dagger, Profile::WarAxe, Profile::Mace,
			Profile::Greatsword, Profile::Battleaxe, Profile::Warhammer,
			Profile::Bow, Profile::Crossbow
		};
		constexpr std::array colors = {
			"Blue.nif", "Bound.nif", "DefaultYellow.nif", "Fire.nif", "Frost.nif",
			"Green.nif", "Purple.nif", "Red.nif", "Shock.nif", "Sun.nif"
		};
		for (const auto profile : profiles) {
			const auto folder = ProfileFolder(profile);
			Check(!folder.empty(), "a nondefault profile has no asset folder");
			for (const auto color : colors) {
				const auto input = std::string("Effects/WeaponTrails/ElementalDesert/") + color;
				const auto expected = std::string("Effects/WeaponTrails/ElementalDesert/WeaponStyles/") + std::string(folder) + '/' + color;
				Check(MapMeshPath(input, profile) == expected, "a weapon style lost its existing enchantment color");
				Check(MapMeshPath(input, Profile::Default) == input, "a mod weapon lost its existing default/enchantment mesh");
			}
		}
		Check(ProfileFolder(Profile::Default).empty(), "default profile must have no replacement folder");
		Check(ProfileFolder(static_cast<Profile>(255)).empty(), "invalid profile must not generate an asset path");
		Check(MapMeshPath("effects\\weapontrails\\elementaldesert\\rEd.NIF", Profile::Dagger) ==
			"Effects/WeaponTrails/ElementalDesert/WeaponStyles/Dagger/Red.nif", "NIF matching must tolerate Windows separators and casing");
	}

	void CustomAndLegacyPathsStayUntouched()
	{
		for (const auto path : {
			"", "Effects/OtherMod/Red.nif", "Meshes/Effects/WeaponTrails/ElementalDesert/Red.nif",
			"Effects/WeaponTrails/ElementalDesert/Custom.nif", "Effects/WeaponTrails/ElementalDesert/Red.nif.bak",
			"Effects/WeaponTrails/ElementalDesert/Density75/Red.nif", "Effects/WeaponTrails/ElementalDesert/WeaponStyles/Bow/Red.nif",
			"Effects/WeaponTrails/ElementalDesert/../Red.nif", "Effects/WeaponTrails/ElementalDesertX/Red.nif"
		}) {
			Check(MapMeshPath(path, Profile::Sword) == path, "path mapping replaced another mod's or a legacy mesh");
		}
		Check(MapMeshPath("Effects/WeaponTrails/ElementalDesert/Red.nif", static_cast<Profile>(255)) ==
			"Effects/WeaponTrails/ElementalDesert/Red.nif", "unknown profile should preserve the original mesh");
	}

	void RapidComboProxyKeepsTheEquippedBowsVisualProfile()
	{
		constexpr std::uint32_t proxy = 0x0400082F, vanillaBow = 0x00013995, modBow = 0x05001234;
		unsigned equipmentReads = 0;
		std::uint32_t equipped = vanillaBow;
		const auto readEquipped = [&] { ++equipmentReads; return equipped; };
		const auto profile = [](std::uint32_t id) {
			return id == vanillaBow ? Resolve("Skyrim.esm", 7) :
				id == modBow ? Resolve("CustomBows.esp", 7) : Resolve("BowRapidCombo.esp", 9);
		};
		const auto resolvedVanilla = BowVisualWeaponID(proxy, proxy, readEquipped);
		Check(resolvedVanilla == vanillaBow && profile(resolvedVanilla) == Profile::Bow && equipmentReads == 1,
			"a recognized rapid-combo proxy replaced the equipped vanilla bow's profile with the mod fallback");
		equipped = modBow;
		const auto resolvedMod = BowVisualWeaponID(proxy, proxy, readEquipped);
		Check(resolvedMod == modBow && profile(resolvedMod) == Profile::Default && equipmentReads == 2,
			"a real mod-created equipped bow acquired a vanilla profile through its launch proxy");
		equipped = 0;
		Check(BowVisualWeaponID(proxy, proxy, readEquipped) == proxy && equipmentReads == 3,
			"a rapid-combo proxy with no actual equipped bow lost its original fallback source");
		equipped = vanillaBow;
		for (const auto source : { vanillaBow, modBow, 0u, 0x0600082Fu }) {
			Check(BowVisualWeaponID(source, proxy, readEquipped) == source && equipmentReads == 3,
				"an unrelated source queried current equipment or inherited another bow's visual profile");
		}
		Check(BowVisualWeaponID(proxy, 0, readEquipped) == proxy && BowVisualWeaponID(0, 0, readEquipped) == 0 && equipmentReads == 3,
			"an absent rapid-combo compatibility form activated source substitution");
	}
}

static_assert(Redsand::Weapons::Resolve({ "Skyrim.esm", 6, true }) == Redsand::Weapons::Profile::Warhammer);
static_assert(Redsand::Weapons::Resolve({ "AnotherMod.esp", 6, true }) == Redsand::Weapons::Profile::Default);
static_assert(Redsand::Weapons::ProfileFolder(Redsand::Weapons::Profile::WarAxe) == "WarAxe");

int main()
{
	try {
		EveryVanillaWeaponCategoryHasItsOwnProfile();
		OriginMatchingCannotAdoptModWeapons();
		LocalizationAndLoadOrderCannotChangeAProfile();
		EveryReleasedColorMapsWithoutLosingEnchantmentColor();
		CustomAndLegacyPathsStayUntouched();
		RapidComboProxyKeepsTheEquippedBowsVisualProfile();
		std::cout << "PASS: vanilla weapon profiles, localized/load-order independence, mod fallback and color-preserving mesh routing\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
