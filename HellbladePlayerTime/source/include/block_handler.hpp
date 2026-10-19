#pragma once

#include <TrueDirectionalMovementAPI.h>

#include "easings.hpp"
#include "slowdown_helper.hpp"
#include <array>

namespace PRECISION_API
{
    class IVPrecision3;
    struct PrecisionHitData;
    struct PreHitCallbackReturn;
}

class BlockHandler final : public RE::BSTEventSink<SKSE::ActionEvent>, public RE::BSTEventSink<RE::BSAnimationGraphEvent>
{
public:
    static BlockHandler& Singleton()
    {
        static BlockHandler self;
        return self;
    }

    bool TryBlock(RE::Actor* attacker, RE::Actor* target);
    PRECISION_API::PreHitCallbackReturn TryBlock(const PRECISION_API::PrecisionHitData& hitData);
    void Update();
    void Reset();
    void RegisterAttackEvents();
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* event,
        RE::BSTEventSource<SKSE::ActionEvent>*) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    void RequestModAPIs();
    bool TryRegisterPrecisionCallback();

    BlockHandler() noexcept = default;
    BlockHandler(const BlockHandler&) = delete;
    BlockHandler(BlockHandler&&) = delete;
    BlockHandler& operator=(const BlockHandler&) = delete;
    BlockHandler& operator=(BlockHandler&&) = delete;

    static inline std::atomic_bool Hit { false };
private:
    bool TryBlockInternal(RE::Actor* attacker, RE::Actor* target, const RE::NiPoint3& hitPos = RE::NiPoint3::Zero());
    void RefreshAttackGraph(RE::PlayerCharacter* player);
    void DetachAttackGraph();
    void QueueSwing(HellbladeTime::SwingSignal signal,std::uintptr_t graphSource=0);

    float deltaTime;
    float deltaTimeRealTime;
    float timeSpentBlocking;
    float timeCooldownRemaining;
    slowdown_helper<easings::testing<float>> slowHelper;
    std::mutex attackEventLock;
    std::array<double,8> pendingAttackTimes{};
    unsigned pendingAttackCount=0;
    HellbladeTime::SwingSignals swingSignals;
    RE::BSTSmartPointer<RE::BShkbAnimationGraph> attackGraph;
    std::atomic_uintptr_t attackGraphSource{0};
    double nextGraphRefresh=0;
    unsigned loggedSwings=0,loggedRolls=0;
    bool loggedUpdate=false,loggedEntry=false;
    HellbladeTime::AttackGate attackGate;
    bool attackEventsRegistered=false;
    std::uint64_t attackRandom=0;
    std::uint32_t loggedSession=0;

    static inline struct
    {
        struct {
            PRECISION_API::IVPrecision3* g_ptr;
            bool isPreHitRegistered;
        } Precision;

        struct {
            TDM_API::IVTDM2* g_ptr;
            bool IsActorTarget(RE::Actor* actor) { return g_ptr && actor && g_ptr->GetCurrentTarget() == actor->GetHandle(); };
        } TDM;
    } API { };
};
