#pragma once

#include <array>
#include <string>
#include <string_view>
#include <cstdint>

namespace RE { class InventoryEntryData; class Actor; }
namespace RedsandEnchantmentProfiles
{
	enum class Category : unsigned { None, Fire, Frost, Shock, Green, Purple, Blue, Red, Bound, Sun, Unknown, Count };
	inline constexpr std::array<std::string_view, 11> categoryKeys{
		"none", "fire", "frost", "shock", "green", "purple", "blue", "red", "bound", "sun", "unknown" };
	constexpr std::uint32_t Bit(Category category) { return 1u << static_cast<unsigned>(category); }
	struct Facts
	{
		bool hasMagic = false, depleted = false, poison = false, bound = false, sun = false;
		std::uint32_t matches = 0;
	};
	constexpr Category Select(const Facts& facts)
	{
		if (!facts.hasMagic || facts.depleted) { return Category::None; }
		if (facts.bound) { return Category::Bound; }
		if (facts.sun) { return Category::Sun; }
		for (auto category : { Category::Green, Category::Purple, Category::Blue, Category::Red, Category::Fire, Category::Frost, Category::Shock }) {
			if (facts.matches & Bit(category)) { return category; }
		}
		return facts.poison ? Category::Green : Category::Unknown;
	}
	std::string Key(RE::InventoryEntryData* entry, RE::Actor* actor, bool left);
}
