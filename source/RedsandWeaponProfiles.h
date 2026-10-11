#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace RE
{
	class TESObjectWEAP;
}

namespace Redsand::Weapons
{
	enum class Profile : std::uint8_t
	{
		Default,
		Sword,
		Dagger,
		WarAxe,
		Mace,
		Greatsword,
		Battleaxe,
		Warhammer,
		Bow,
		Crossbow
	};

	struct Descriptor
	{
		std::string_view originPlugin;
		std::uint32_t type = 0;
		bool warhammerKeyword = false;
	};

	namespace detail
	{
		[[nodiscard]] constexpr char LowerASCII(char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
		}

		[[nodiscard]] constexpr bool EqualASCII(std::string_view first, std::string_view second) noexcept
		{
			if (first.size() != second.size()) {
				return false;
			}
			for (std::size_t index = 0; index < first.size(); ++index) {
				if (LowerASCII(first[index]) != LowerASCII(second[index])) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] constexpr bool EqualMeshPath(std::string_view first, std::string_view second) noexcept
		{
			if (first.size() != second.size()) {
				return false;
			}
			for (std::size_t index = 0; index < first.size(); ++index) {
				const auto a = first[index] == '\\' ? '/' : LowerASCII(first[index]);
				const auto b = second[index] == '\\' ? '/' : LowerASCII(second[index]);
				if (a != b) {
					return false;
				}
			}
			return true;
		}
	}

	[[nodiscard]] constexpr Profile Resolve(Descriptor weapon) noexcept
	{
		if (!detail::EqualASCII(weapon.originPlugin, "Skyrim.esm") &&
			!detail::EqualASCII(weapon.originPlugin, "Dawnguard.esm") &&
			!detail::EqualASCII(weapon.originPlugin, "Dragonborn.esm")) {
			return Profile::Default;
		}
		switch (weapon.type) {
		case 1: return Profile::Sword;
		case 2: return Profile::Dagger;
		case 3: return Profile::WarAxe;
		case 4: return Profile::Mace;
		case 5: return Profile::Greatsword;
		case 6: return weapon.warhammerKeyword ? Profile::Warhammer : Profile::Battleaxe;
		case 7: return Profile::Bow;
		case 8: return Profile::Default;
		case 9: return Profile::Crossbow;
		default: return Profile::Default;
		}
	}

	[[nodiscard]] constexpr Profile Resolve(std::string_view originPlugin, std::uint32_t type, bool warhammerKeyword = false) noexcept
	{
		return Resolve(Descriptor{ originPlugin, type, warhammerKeyword });
	}

	template <class EquippedBowID>
	[[nodiscard]] std::uint32_t BowVisualWeaponID(std::uint32_t source, std::uint32_t proxy, EquippedBowID equippedBowID)
	{
		if (proxy && source == proxy) {
			if (const auto equipped = equippedBowID()) { return equipped; }
		}
		return source;
	}

	[[nodiscard]] constexpr std::string_view ProfileFolder(Profile profile) noexcept
	{
		switch (profile) {
		case Profile::Sword: return "Sword";
		case Profile::Dagger: return "Dagger";
		case Profile::WarAxe: return "WarAxe";
		case Profile::Mace: return "Mace";
		case Profile::Greatsword: return "Greatsword";
		case Profile::Battleaxe: return "Battleaxe";
		case Profile::Warhammer: return "Warhammer";
		case Profile::Bow: return "Bow";
		case Profile::Crossbow: return "Crossbow";
		default: return {};
		}
	}

	[[nodiscard]] inline std::string MapMeshPath(std::string_view path, Profile profile)
	{
		constexpr std::string_view root = "RedsandParticles/";
		constexpr std::string_view legacy = "Effects/WeaponTrails/ElementalDesert/";
		constexpr std::array<std::string_view, 10> colors = {
			"Blue.nif", "Bound.nif", "DefaultYellow.nif", "Fire.nif", "Frost.nif",
			"Green.nif", "Purple.nif", "Red.nif", "Shock.nif", "Sun.nif"
		};
		if (profile > Profile::Crossbow) { return std::string(path); }
		auto filename = path;
		if (detail::EqualMeshPath(path, "RedsandParticles/Default.nif")) {
			filename = "DefaultYellow.nif";
		} else if (path.size() > legacy.size() && detail::EqualMeshPath(path.substr(0, legacy.size()), legacy)) {
			filename.remove_prefix(legacy.size());
			constexpr std::string_view styles = "WeaponStyles/";
			if (filename.size() > styles.size() && detail::EqualMeshPath(filename.substr(0, styles.size()), styles)) {
				filename.remove_prefix(styles.size());
				const auto separator = filename.find_first_of("/\\");
				if (separator == std::string_view::npos) { return std::string(path); }
				const auto folder = filename.substr(0, separator);
				bool found = false;
				for (unsigned value = 1; value <= static_cast<unsigned>(Profile::Crossbow); ++value) {
					const auto candidate = static_cast<Profile>(value);
					if (detail::EqualASCII(folder, ProfileFolder(candidate))) {
						profile = candidate;
						found = true;
						break;
					}
				}
				if (!found) { return std::string(path); }
				filename.remove_prefix(separator + 1);
			}
		} else {
			return std::string(path);
		}
		for (const auto color : colors) {
			if (detail::EqualASCII(filename, color)) {
				std::string result(root);
				result += profile == Profile::Default ? "Default" : ProfileFolder(profile);
				result += ".nif";
				return result;
			}
		}
		return std::string(path);
	}

	[[nodiscard]] Profile ForWeapon(RE::TESObjectWEAP* weapon);
}
