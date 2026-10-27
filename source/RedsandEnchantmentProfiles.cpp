#include "RedsandEnchantmentProfiles.h"
#include "RedsandMeleeStyle.h"
#include "RE/Skyrim.h"
#include "Offsets.h"
#include <cmath>

namespace RedsandEnchantmentProfiles
{
	std::string Key(RE::InventoryEntryData* entry, RE::Actor* actor, bool left)
	{
		Facts facts;
		auto* magic = entry ? GetPoison(entry) : nullptr;
		facts.poison = magic != nullptr;
		if (!magic && entry) { magic = GetEnchantment(entry); }
		facts.hasMagic = magic != nullptr;
		if (!magic) { return "none"; }
		const auto charge = entry->GetEnchantmentCharge();
		bool costAvailable = false;
		float available = 0.f, cost = 0.f;
		if (!facts.poison && actor) {
			const auto source = left ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
			const auto value = GetActorValueForCost(magic, source);
			if (value != RE::ActorValue::kNone) {
				available = actor->AsActorValueOwner()->GetActorValue(value); cost = magic->CalculateMagickaCost(actor);
				costAvailable = std::isfinite(available) && std::isfinite(cost);
			}
		}
		facts.depleted = Redsand::Weapons::InsufficientEnchantmentCharge(facts.poison, true, charge.has_value(), charge.value_or(0.), costAvailable, available, cost);
		if (const auto* dominant = magic->GetCostliestEffectItem(); dominant && dominant->baseEffect) {
			const std::string_view name = dominant->baseEffect->GetName();
			facts.bound = name.find("Bound") != name.npos && name.find("FX") != name.npos;
		}
		const std::string_view weapon = entry->object ? entry->object->GetName() : "";
		const std::string_view enchantment = magic->GetName();
		for (auto name : { "Dawnbreaker", "破晓者", "破晓" }) { facts.sun |= weapon.find(name) != weapon.npos; }
		for (auto name : { "Sun", "Dawnbreaker", "破晓者" }) { facts.sun |= enchantment.find(name) != enchantment.npos; }
		for (auto* item : magic->effects) {
			auto* effect = item ? item->baseEffect : nullptr;
			if (!effect) { continue; }
			const std::string_view name = effect->GetName();
			facts.sun |= name.find("Sun Damage") != name.npos || name.find("阳光伤害") != name.npos || effect->HasKeywordString("MagicTurnUndead");
			if (effect->HasKeywordString("MagicDamageFire")) { facts.matches |= Bit(Category::Fire); }
			if (effect->HasKeywordString("MagicDamageFrost")) { facts.matches |= Bit(Category::Frost); }
			if (effect->HasKeywordString("MagicDamageShock")) { facts.matches |= Bit(Category::Shock); }
			if (auto* shader = effect->data.enchantShader) {
				auto* file = shader->GetFile(0);
				if (file && _stricmp(file->fileName, "Skyrim.esm") == 0) {
					switch (shader->GetLocalFormID()) {
					case 0x5D606: facts.matches |= Bit(Category::Green); break;
					case 0x5D607: facts.matches |= Bit(Category::Purple); break;
					case 0x5D608: facts.matches |= Bit(Category::Blue); break;
					case 0xD7661: facts.matches |= Bit(Category::Red); break;
					case 0x4E810: facts.matches |= Bit(Category::Fire); break;
					case 0x4FA1D: facts.matches |= Bit(Category::Frost); break;
					case 0x59EDA: facts.matches |= Bit(Category::Shock); break;
					}
				}
			}
		}
		return std::string(categoryKeys[static_cast<unsigned>(Select(facts))]);
	}
}
