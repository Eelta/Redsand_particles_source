#include "Events.h"
#include "Functions.h"
#include "DataHandler.h"
#include "RedsandBlockBridge.h"


RE::BSTempEffectParticle* TESObjectCELL_PlaceParticleEffect(RE::TESObjectCELL* a_cell, float a_lifetime, const char* a_modelName, const RE::NiMatrix3& a_normal, const RE::NiPoint3& a_pos, float a_scale, std::uint32_t a_flags, RE::NiAVObject* a_target)
{
	using func_t = decltype(&TESObjectCELL_PlaceParticleEffect);
	REL::Relocation<func_t> func{ REL::RelocationID(29219, 30072) };
	return func(a_cell, a_lifetime, a_modelName, a_normal, a_pos, a_scale, a_flags, a_target);
}

namespace MaxsuBlockSpark
{

	EventResult OnHitEventHandler::ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>* a_eventSource)
	{
		using HitFlag = RE::TESHitEvent::Flag;

		if (!a_event || !a_eventSource) {
			ERROR("Event Source Not Found!");
			return EventResult::kContinue;
		}

		logger::debug("OnHit Event Trigger!");

		if (a_event->flags.any(HitFlag::kHitBlocked) && a_event->target) {
			auto defender = a_event->target->As<RE::Actor>();
			auto attackWeapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_event->source);
			if (!defender || !attackWeapon || !defender->GetActorRuntimeData().currentProcess || !defender->GetActorRuntimeData().currentProcess->high || !attackWeapon->IsMelee() || attackWeapon->IsHandToHandMelee() || !defender->Get3D())
				return EventResult::kContinue;

			auto attacker = a_event->cause ? a_event->cause->As<RE::Actor>(): nullptr;
			if (!attacker || !attacker->GetActorRuntimeData().currentProcess || !attacker->GetActorRuntimeData().currentProcess->high) {
				logger::debug("Attack Actor Not Found!");
				return EventResult::kContinue;
			}

			auto attackerData = attacker->GetActorRuntimeData().currentProcess->high->attackData;
			if (!attackerData) {
				logger::debug("Attacker Attack Data Not Found!");
				return EventResult::kContinue;
			}

			auto attackerNode = attackerData->IsLeftAttack() ? attacker->GetNodeByName("SHIELD") : attacker->GetNodeByName("WEAPON");
			if (!attackerNode) {
				logger::debug("Attacker Node Not Found!");
				return EventResult::kContinue;
			}

			auto GetBipeObjIndex = [](RE::TESForm* parryEquipment, bool rightHand) -> RE::BIPED_OBJECT {
				if (!parryEquipment)
					return RE::BIPED_OBJECT::kNone;

				if (parryEquipment->As<RE::TESObjectWEAP>() ) {
					switch (parryEquipment->As<RE::TESObjectWEAP>()->GetWeaponType()) {
					case RE::WEAPON_TYPE::kOneHandSword:
						return rightHand ? RE::BIPED_OBJECT::kOneHandSword : RE::BIPED_OBJECT::kShield;

					case RE::WEAPON_TYPE::kOneHandAxe:
						return rightHand ? RE::BIPED_OBJECT::kOneHandAxe : RE::BIPED_OBJECT::kShield;

					case RE::WEAPON_TYPE::kOneHandMace:
						return rightHand ? RE::BIPED_OBJECT::kOneHandMace : RE::BIPED_OBJECT::kShield;

					case RE::WEAPON_TYPE::kTwoHandAxe:
					case RE::WEAPON_TYPE::kTwoHandSword:
						return RE::BIPED_OBJECT::kTwoHandMelee;
					}
				} else if (parryEquipment->IsArmor())
					return RE::BIPED_OBJECT::kShield;

				return RE::BIPED_OBJECT::kNone;
			};

			RE::BIPED_OBJECT BipeObjIndex;
			auto defenderLeftEquipped = defender->GetActorRuntimeData().currentProcess->GetEquippedLeftHand();
			auto defenderData = defender->GetActorRuntimeData().currentProcess->high->attackData;

			if (defenderData)
				BipeObjIndex = defenderData->IsLeftAttack() && defenderLeftEquipped ? GetBipeObjIndex(defenderLeftEquipped, false) : GetBipeObjIndex(defender->GetActorRuntimeData().currentProcess->GetEquippedRightHand(), true);
			else
				BipeObjIndex = defenderLeftEquipped && (defenderLeftEquipped->IsWeapon() || defenderLeftEquipped->IsArmor()) ? GetBipeObjIndex(defenderLeftEquipped, false) : GetBipeObjIndex(defender->GetActorRuntimeData().currentProcess->GetEquippedRightHand(), true);

			if (BipeObjIndex == RE::BIPED_OBJECT::kNone) {
				logger::debug("BipeObj Not Found!");
				return EventResult::kContinue;
			}

				logger::debug("Defender BipeObjIndex is {}", static_cast<std::uint32_t>(BipeObjIndex));

			auto defenderNode = defender->GetCurrentBiped()->objects[BipeObjIndex].partClone;
			if (!defenderNode || !defenderNode.get()) {
				logger::debug("Defender Node Not Found!");
				return EventResult::kContinue;
			}

			auto cell = defender->GetParentCell();

			const auto modelName = BipeObjIndex == RE::BIPED_OBJECT::kShield && defenderLeftEquipped && defenderLeftEquipped->IsArmor() ? "Maxsu\\SimpleBlockSpark\\fxmetalsparkimpactshield.nif" : "Maxsu\\SimpleBlockSpark\\fxmetalsparkimpactweap.nif";

			RE::NiPoint3 sparkPos;
			RE::NiPoint3 hitPos = attackerNode->worldBound.center + attackerNode->world.rotate * RE::NiPoint3(0.f,0.5f * attackerNode->worldBound.radius,0.f);

			if (BipeObjIndex == RE::BIPED_OBJECT::kShield && defenderLeftEquipped && defenderLeftEquipped->IsArmor() && SparkLocalizer::GetShieldSparkPos(hitPos, defenderNode.get(), sparkPos))
				logger::debug("Get Shield Spark Position!");
			else {
				sparkPos = defenderNode->worldBound.center;
				logger::debug("Get Weapon Spark Position!");
			}
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<> distrib(1, 100);

			if (distrib(gen) > DataHandler::GetSingleton()->settings->triggerChance) {
				logger::debug("Spark Random Chance Not Enough!");
				return EventResult::kContinue;
			}

			static std::atomic_bool loggedAPI{ false };
			if (RedsandBlockBridge::Resolve() && !loggedAPI.exchange(true)) {
				logger::info("Redsand block compatibility API found");
			}
			const auto selected = RedsandBlockBridge::Select(modelName);
			static std::atomic_bool loggedCustom{ false };
			if (selected.custom && !loggedCustom.exchange(true)) {
				logger::info("Redsand block compatibility active: {}", selected.model);
			}
			if (TESObjectCELL_PlaceParticleEffect(cell, selected.lifetime, selected.model, defenderNode->world.rotate, sparkPos, 1.0f, RedsandBlockBridge::SpawnFlags(selected), defenderNode.get())) {
				logger::debug("Play Spark Effect Successfully!");
				return EventResult::kContinue;
			}

			logger::debug("Play Spark Effect Fail!");
		}

		return EventResult::kContinue;
	}

}
