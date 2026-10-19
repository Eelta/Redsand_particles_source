#include "hooks.hpp"
#include "block_handler.hpp"
#include "util.hpp"

struct HitData_GetBashDamage
{
    static void thunk(RE::ActorValueOwner* avOwner, float& damage)
    {
        func(avOwner, damage);
        damage *= BlockHandler::Hit ? 0.f : 1.f;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct HitData_GetUnarmedDamage
{
    static void thunk(RE::ActorValueOwner* avOwner, float& damage)
    {
        func(avOwner, damage);
        damage *= BlockHandler::Hit ? 0.f : 1.f;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct HitData_GetWeaponDamage
{
    static void thunk(RE::InventoryEntryData* weapon, RE::ActorValueOwner* avOwner, float damageMult, bool a4)
    {
        func(weapon, avOwner, BlockHandler::Hit ? 0.f : damageMult, a4);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct ApplyAttackSpells
{
    static void thunk(RE::Actor* self, RE::InventoryEntryData* entry, bool isLeftHand, RE::Actor* target)
    {
        if (BlockHandler::Hit) {
            return;
        }

        func(self, entry, isLeftHand, target);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct GetBlockCost
{
    static float thunk(RE::HitData& hitData)
    {
        return BlockHandler::Hit ? 0.f : func(hitData);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct MeleeHit
{
    static void thunk(RE::Actor* target, RE::HitData& hitData)
    {
        if (BlockHandler::Hit) {
            hitData.stagger = static_cast<uint32_t>(0.f);
            hitData.percentBlocked = 1.f;
            hitData.physicalDamage = 0.f;
            hitData.totalDamage = 0.f;
        }

        func(target, hitData);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct MeleeCollision
{
    static void thunk(RE::Actor* attacker, RE::Actor* target, int64_t unk_1, bool unk_2, void* unk_3)
    {
        if (BlockHandler::Singleton().TryBlock(attacker, target))
            return;

        func(attacker, target, unk_1, unk_2, unk_3);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct PlayerCharacter_Update
{
    static void thunk(RE::PlayerCharacter* self, float delta)
    {
        func(self, delta);
        BlockHandler::Singleton().Update();
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

void Hooks::Install()
{
    const uintptr_t addr1 = Offsets::HitData_Populate.address();
    stl::write_thunk_call<HitData_GetBashDamage>(addr1 + REL::Relocate(0x22F, 0x226));
    stl::write_thunk_call<HitData_GetUnarmedDamage>(addr1 + REL::Relocate(0x26A, 0x24D));
    stl::write_thunk_call<HitData_GetWeaponDamage>(addr1 + REL::Relocate(0x1A5, 0x1A4));

    const uintptr_t addr2 = Offsets::Actor_DealDamage.address();
    stl::write_thunk_call<ApplyAttackSpells>(addr2 + REL::Relocate(0x185, 0x194));
    stl::write_thunk_call<MeleeHit>(addr2 + REL::Relocate(0x3C0, 0x4A8, 0x3C0));

    stl::write_thunk_call<GetBlockCost>(REL::RelocationID(37633, 38586).address() + REL::Relocate(0x8D4, 0xB39));
    stl::write_thunk_call<MeleeCollision>(REL::RelocationID(37650, 38603).address() + REL::Relocate(0x38B, 0x45A));

    stl::write_vfunc<RE::PlayerCharacter, 0xAD, PlayerCharacter_Update>();
};
