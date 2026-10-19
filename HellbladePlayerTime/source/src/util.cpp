#include "util.hpp"
#include "RE/ActorMediator.hpp"

void* Util::Actor_DealDamage(RE::Actor* aggressor, RE::Actor* target, RE::Projectile* sourceProjectile, bool isLeftHand)
{
    REL::Relocation<decltype(&Actor_DealDamage)> func { Offsets::Actor_DealDamage };
    return func(aggressor, target, sourceProjectile, isLeftHand);
}

RE::TESObjectREFR* Util::Actor_GetEquippedShield(RE::Actor* actor)
{
    REL::Relocation<decltype(&Actor_GetEquippedShield)> func { Offsets::Actor_GetEquippedShield };
    return func(actor);
}

RE::InventoryEntryData* Util::AIProcess_GetCurrentlyEquippedWeapon(RE::AIProcess* self, bool isLeftHand)
{
    REL::Relocation<decltype(&AIProcess_GetCurrentlyEquippedWeapon)> func { Offsets::AIProcess_GetCurrentlyEquippedWeapon };
    return func(self, isLeftHand);
}

RE::BGSImpactData* Util::BGSImpactDataSet_GetImpactData(RE::BGSImpactDataSet* impactSet, RE::BGSMaterialType* material)
{
    REL::Relocation<decltype(&BGSImpactDataSet_GetImpactData)> func { Offsets::BGSImpactDataSet_GetImpactData };
    return func(impactSet, material);
}

void Util::CastSpell(RE::Actor* caster, RE::Actor* target, RE::SpellItem* spell)
{
    using RE::MagicSystem::CastingSource;
    using RE::MagicSystem::SpellType;

    switch (spell->GetSpellType()) {
        case SpellType::kDisease:
        case SpellType::kAbility:
        case SpellType::kAddiction:
            target->AddSpell(spell);
            break;
        default:
            caster->GetMagicCaster(CastingSource::kInstant)->CastSpellImmediate(spell, false, target, 1.f, false, 0.f, caster);
    }
}

RE::NiAVObject* Util::GetNiObjectFromCollidable(const RE::hkpCollidable* collidable) {
    using func_t = decltype(&GetNiObjectFromCollidable);
    REL::Relocation<func_t> func { Offsets::NiObjectFromCollidable };
    return func(collidable);
}

void Util::HitData_Ctor(RE::HitData* self)
{
    using func_t = decltype(&HitData_Ctor);
    REL::Relocation<func_t> func { Offsets::HitData_Ctor };
    func(self);
}
void Util::HitData_Populate(RE::HitData* self, RE::Actor* a_aggressor, RE::Actor* a_target, RE::InventoryEntryData* a_weapon, bool isLeftHand)
{
    using func_t = decltype(&HitData_Populate);
    REL::Relocation<func_t> func { Offsets::HitData_Populate };
    return func(self, a_aggressor, a_target, a_weapon, isLeftHand);
}

bool Util::IsInBlockAngle(RE::Actor* defender, RE::TESObjectREFR* refr)
{
    if (defender && refr) {
        const auto gameSettings = RE::GameSettingCollection::GetSingleton();
        const auto setting = gameSettings ? gameSettings->GetSetting("fCombatHitConeAngle") : nullptr;

        if (const float blockAngle = std::abs(setting ? setting->GetFloat() : 35.f)) {
            const float heading = defender->GetHeadingAngle(refr->GetPosition(), true);
            return ApproximatelyLessOrEqual(heading, blockAngle);
        }
    }

    return false;
}

bool Util::PerformAction(RE::BGSAction* action, RE::Actor* actor, RE::TESObjectREFR* target)
{
    if (action && actor) {
        std::unique_ptr<RE::TESActionData> actionData(RE::TESActionData::Create());
        actionData->source = actor->GetHandle().get();
        actionData->action = action;
        actionData->target = target ? target->GetHandle().get() : nullptr;
        actionData->Process();





        REL::Relocation<bool(RE::TESActionData*)> func { Offsets::PerformAction };
        return func(actionData.get());


    }

    return false;
}

bool Util::TryPlaySoundAt(const RE::TESObjectREFR* refr, RE::BGSSoundDescriptorForm* descriptor) {
    if (!refr || !descriptor)
        return false;

    const auto soundCategory = descriptor->soundDescriptor->category;
    return TryPlaySoundAt(refr, descriptor, soundCategory ? soundCategory->GetCategoryVolume() : 1.f);
}

bool Util::TryPlaySoundAt(const RE::TESObjectREFR* refr, RE::BGSSoundDescriptorForm* descriptor, float volume)
{
    if (!refr || !descriptor)
        return false;

    RE::BSSoundHandle sound;
    sound.soundID = RE::BSSoundHandle::kInvalidID;
    sound.assumeSuccess = false;
    sound.state = RE::BSSoundHandle::AssumedState::kPlaying;

    if (const auto manager = RE::BSAudioManager::GetSingleton()) {
        if (!manager->GetSoundHandle(sound, descriptor))
            return false;

        if (!sound.SetPosition(refr->GetPosition()))
            return false;

        sound.SetObjectToFollow(refr->Get3D());
        sound.SetVolume(volume);
    }

    return sound.Play();
}

void Util::TryStagger(RE::Actor* target, float staggerMult, RE::Actor* aggressor)
{
    using func_t = decltype(&TryStagger);
    REL::Relocation<func_t> func { Offsets::TryStagger };
    func(target, staggerMult, aggressor);
}

void Util::SetGlobalTimeMult(float multiplier)
{
    REL::Relocation<RE::BSTimer*> singleton { Offsets::BSTimer_GetSingleton };
    REL::Relocation<void(RE::BSTimer*, float, bool)> func { Offsets::BSTimer_SetGlobalTimeMultiplier };

    func(singleton.get(), multiplier, false);
}

float& Util::GetGlobalTimeMult()
{
    static REL::Relocation<float*> value{RELOCATION_ID(511882,388442)};
    return *value;
}
