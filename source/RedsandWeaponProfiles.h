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
		constexpr std::string_view root = "Effects/WeaponTrails/ElementalDesert/";
		constexpr std::array<std::string_view, 10> colors = {
			"Blue.nif", "Bound.nif", "DefaultYellow.nif", "Fire.nif", "Frost.nif",
			"Green.nif", "Purple.nif", "Red.nif", "Shock.nif", "Sun.nif"
		};
		const auto folder = ProfileFolder(profile);
		if (folder.empty() || path.size() <= root.size() ||
			!detail::EqualMeshPath(path.substr(0, root.size()), root)) {
			return std::string(path);
		}
		const auto filename = path.substr(root.size());
		for (const auto color : colors) {
			if (detail::EqualASCII(filename, color)) {
				std::string result(root);
				result += "WeaponStyles/";
				result += folder;
				result += '/';
				result += color;
				return result;
			}
		}
		return std::string(path);
	}

	[[nodiscard]] Profile ForWeapon(RE::TESObjectWEAP* weapon);
}
