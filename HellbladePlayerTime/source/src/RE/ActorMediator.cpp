#include "RE/ActorMediator.hpp"

bool RE::ActorMediator::PerformAction(RE::TESActionData* actionData) noexcept
{
    REL::Relocation<uint8_t(void*, RE::TESActionData*)> func { RELOCATION_ID(37996, 38949) };
    return func(this, actionData) != 0;
}

bool RE::ActorMediator::ForceAction(RE::TESActionData* actionData) noexcept
{
    REL::Relocation<void*(void*, RE::TESActionData*)> ApplyAnimationVariables { RELOCATION_ID(38048, 39004) };
    REL::Relocation<uint8_t(ActorMediator*, RE::TESActionData*)> PerformComplexAction { RELOCATION_ID(0, 38953) };

    void** qword_142F271B8 = (void**)RELOCATION_ID(0, 403566).address();

    uint8_t result = 0;
    const auto actor = skyrim_cast<RE::Actor*>(actionData->source.get());

    RE::BSAnimationGraphManagerPtr graphManager;
    if (actor && actor->GetAnimationGraphManager(graphManager)) {
        result = PerformComplexAction(this, actionData);
        ApplyAnimationVariables(*qword_142F271B8, actionData);
    }


    return result;
}
