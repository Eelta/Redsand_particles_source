#pragma once

namespace RE
{
    struct ActorMediator
    {
        static ActorMediator* GetSingleton()
        {
            REL::Relocation<ActorMediator*> singleton { RELOCATION_ID(517059, 403567) };
            return singleton.get();
        }

        virtual ~ActorMediator()
        {

        };

        bool PerformAction(RE::TESActionData* actionData) noexcept;
        bool ForceAction(RE::TESActionData* actionData) noexcept;
    };
}
