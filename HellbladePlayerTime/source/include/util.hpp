#pragma once



template <typename T> concept arithmetic = std::is_arithmetic_v<T>;

namespace Offsets
{
    static float* g_DeltaTime = (float*)REL::VariantID(523660, 410199, 0x30C3A08).address();
    static float* g_DeltaRealTime = (float*)REL::VariantID(523661, 410200, 0x30C3A0C).address();

    inline constexpr REL::RelocationID Actor_DealDamage { 37673, 38627 };
    inline constexpr REL::RelocationID Actor_GetEquippedShield { 37624, 38577 };
    inline constexpr REL::RelocationID AIProcess_GetCurrentlyEquippedWeapon { 38781, 39806 };
    inline constexpr REL::VariantID BGSImpactDataSet_GetImpactData { 20408, 20860, 0x2D4C00 };
    inline constexpr REL::RelocationID BSTimer_GetSingleton { 523657, 410196 };
    inline constexpr REL::RelocationID BSTimer_SetGlobalTimeMultiplier { 66988, 68245 };
    inline constexpr REL::VariantID HitData_Ctor { 42826, 43995, 0x76D000 };
    inline constexpr REL::VariantID HitData_Populate { 42832, 44001, 0x76D400 };
    inline constexpr REL::RelocationID NiObjectFromCollidable { 76160, 77988 };
    inline constexpr REL::RelocationID PerformAction { 40551, 41557 };
    inline constexpr REL::RelocationID PlaceParticleEffect { 29219, 30072 };
    inline constexpr REL::RelocationID PlayIdle { 38290, 39256 };
    inline constexpr REL::RelocationID TryStagger { 36700, 37710 };
};

namespace Util
{
    bool BlockSlowdownSuspended();
    bool BeginBlockSlowdown();
    bool ApplyBlockSlowdown(float world);
    void EndBlockSlowdown();
    template <typename T>
    bool ApproximatelyEqual(const T& a, const T& b, T epsilon = std::numeric_limits<T>::epsilon()) {
        const T ax = std::abs(a);
        const T bx = std::abs(b);

        return std::abs(a - b) <= (ax < bx ? bx : ax) * epsilon;
    }

    template <typename T>
    bool ApproximatelyGreaterOrEqual(const T& a, const T& b, T epsilon = std::numeric_limits<T>::epsilon()) {
        return a > b || ApproximatelyEqual(a, b, epsilon);
    }

    template <typename T>
    bool ApproximatelyLessOrEqual(const T& a, const T& b, T epsilon = std::numeric_limits<T>::epsilon()) {
        return a < b || ApproximatelyEqual(a, b, epsilon);
    }

    template <typename T>
    bool DefinitelyGreater(const T& a, const T& b, T epsilon = std::numeric_limits<T>::epsilon()) {
        const T ax = std::abs(a);
        const T bx = std::abs(b);

        return (a - b) > (ax < bx ? bx : ax) * epsilon;
    }

    template <typename T>
    bool DefinitelyLess(const T& a, const T& b, T epsilon = std::numeric_limits<T>::epsilon()) {
        const T ax = std::abs(a);
        const T bx = std::abs(b);

        return (b - a) > (ax < bx ? bx : ax) * epsilon;
    }

    template <typename T> requires arithmetic<T>
    void Limit(T& in, const T& min, const T& max) {
        if (min > max)
            return;

        LimitLower(in, min);
        LimitHigher(in, max);
    }

    template <typename T> requires arithmetic<T>
    void LimitLower(T& in, const T& min) {
        if (in < min) {
            in = min;
        }
    }

    template <typename T> requires arithmetic<T>
    void LimitHigher(T& in, const T& max) {
        if (in > max) {
            in = max;
        }
    }




























































    auto Actor_DealDamage(RE::Actor* aggressor, RE::Actor* target, RE::Projectile* sourceProjectile, bool isLeftHand = false) -> void*;
    auto Actor_GetEquippedShield(RE::Actor* actor) -> RE::TESObjectREFR*;
    auto AIProcess_GetCurrentlyEquippedWeapon(RE::AIProcess* self, bool isLeftHand = false) -> RE::InventoryEntryData*;
    auto BGSImpactDataSet_GetImpactData(RE::BGSImpactDataSet* impactSet, RE::BGSMaterialType* material) -> RE::BGSImpactData*;
    auto CastSpell(RE::Actor* caster, RE::Actor* target, RE::SpellItem* spell) -> void;
    auto GetNiObjectFromCollidable(const RE::hkpCollidable* collidable) -> RE::NiAVObject*;
    auto HitData_Ctor(RE::HitData* self) -> void;
    auto HitData_Populate(RE::HitData* self, RE::Actor* a_aggressor, RE::Actor* a_target, RE::InventoryEntryData* a_weapon, bool isLeftHand = false) -> void;
    auto IsInBlockAngle(RE::Actor* defender, RE::TESObjectREFR* refr) -> bool;
    auto PerformAction(RE::BGSAction* action, RE::Actor* actor, RE::TESObjectREFR* target = nullptr) -> bool;
    auto TryStagger(RE::Actor* target, float staggerMult, RE::Actor* aggressor) -> void;
    auto TryPlaySoundAt(const RE::TESObjectREFR* refr, RE::BGSSoundDescriptorForm* descriptor) -> bool;
    auto TryPlaySoundAt(const RE::TESObjectREFR* refr, RE::BGSSoundDescriptorForm* descriptor, float volume) -> bool;

    void   SetGlobalTimeMult(float multiplier);
    float& GetGlobalTimeMult();










}
