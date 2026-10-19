#include <PrecisionAPI.h>

#include "forms.hpp"
#include "block_handler.hpp"
#include "settings.hpp"
#include "util.hpp"
#include "time_vfx.hpp"
#include <chrono>
#include <random>
#include <RE/B/BShkbAnimationGraph.h>

namespace {
double RealNow() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}

void BlockHandler::QueueSwing(HellbladeTime::SwingSignal signal,std::uintptr_t graphSource)
{
    std::lock_guard lock(attackEventLock);
    if(graphSource && graphSource!=attackGraphSource.load(std::memory_order_relaxed)) return;
    const double now=RealNow();
    if(!swingSignals.Accept(signal,now) || pendingAttackCount==pendingAttackTimes.size()) return;
    pendingAttackTimes[pendingAttackCount++]=now;
    if(loggedSwings++<8) logger::info("Player attack signal accepted: {}",
        signal==HellbladeTime::SwingSignal::CollisionStart ? "collision-start" :
            signal==HellbladeTime::SwingSignal::GraphSwing ? "animation-swing" : "SKSE-swing");
}

void BlockHandler::DetachAttackGraph()
{
    attackGraphSource.store(0,std::memory_order_relaxed);
    if(attackGraph) attackGraph->RemoveEventSink<RE::BSAnimationGraphEvent>(this);
    attackGraph=nullptr;
}

void BlockHandler::RefreshAttackGraph(RE::PlayerCharacter* player)
{
    const double now=RealNow();
    if(now<nextGraphRefresh) return;
    nextGraphRefresh=now+.25;
    RE::BSAnimationGraphManagerPtr manager;
    if(!player->Is3DLoaded() || !player->GetAnimationGraphManager(manager) || !manager) return;
    const auto active=manager->GetRuntimeData().activeGraph;
    if(active>=manager->graphs.size()) return;
    const auto next=manager->graphs[active];
    if(next.get()==attackGraph.get()) return;
    DetachAttackGraph();
    {std::lock_guard lock(attackEventLock);swingSignals={};}
    attackGraph=next;
    if(attackGraph) {
        attackGraph->AddEventSink<RE::BSAnimationGraphEvent>(this);
        attackGraphSource.store(reinterpret_cast<std::uintptr_t>(attackGraph->GetEventSource<RE::BSAnimationGraphEvent>()),std::memory_order_relaxed);
        logger::info("Player attack animation graph attached: activeGraph={}",active);
    }
}

RE::BSEventNotifyControl BlockHandler::ProcessEvent(const RE::BSAnimationGraphEvent* event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
{
    if(event && event->holder && event->holder->IsPlayerRef() &&
        reinterpret_cast<std::uintptr_t>(source)==attackGraphSource.load(std::memory_order_relaxed)) {
        if(const auto* tag=event->tag.c_str()) {
            if(auto signal=HellbladeTime::ClassifySwing(tag))
                QueueSwing(*signal,reinterpret_cast<std::uintptr_t>(source));
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

void BlockHandler::RegisterAttackEvents()
{
    if (attackEventsRegistered) return;
    if (auto* events=SKSE::GetActionEventSource()) {
        events->AddEventSink(this);
        attackEventsRegistered=true;
        std::random_device random;
        attackRandom=(std::uint64_t(random())<<32)^random();
        logger::info("Registered player weapon-swing events for attack slowdown");
    }
}

RE::BSEventNotifyControl BlockHandler::ProcessEvent(const SKSE::ActionEvent* event,
    RE::BSTEventSource<SKSE::ActionEvent>*)
{
    if (event && event->type==SKSE::ActionEvent::Type::kWeaponSwing &&
        event->actor && event->actor->IsPlayerRef()) {
        const auto* weapon=event->sourceForm ? event->sourceForm->As<RE::TESObjectWEAP>() : nullptr;
        if (!event->sourceForm || (weapon && weapon->IsMelee())) {


            QueueSwing(HellbladeTime::SwingSignal::EngineSwing);
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

bool BlockHandler::TryBlock(RE::Actor* attacker, RE::Actor* target)
{
    if (!attacker || !(target && target->IsPlayerRef()))
        return false;

    auto GetBipedIndex = [target]() {
        if (Util::Actor_GetEquippedShield(target) != nullptr) {
            return RE::BIPED_OBJECT::kShield;
        }

        if (const auto process = target->GetActorRuntimeData().currentProcess) {
            for (int i = 0; i < RE::AIProcess::Hand::kTotal; i++) {
                const auto equipped = process->equippedObjects[i];
                const auto weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;

                if (weapon == nullptr) {
                    continue;
                }

                switch (weapon->GetWeaponType()) {
                    case RE::WEAPON_TYPE::kOneHandSword:
                        return RE::BIPED_OBJECT::kOneHandSword;
                    case RE::WEAPON_TYPE::kOneHandDagger:
                        return RE::BIPED_OBJECT::kOneHandDagger;
                    case RE::WEAPON_TYPE::kOneHandAxe:
                        return RE::BIPED_OBJECT::kOneHandAxe;
                    case RE::WEAPON_TYPE::kOneHandMace:
                        return RE::BIPED_OBJECT::kOneHandMace;
                    case RE::WEAPON_TYPE::kHandToHandMelee:
                    case RE::WEAPON_TYPE::kTwoHandSword:
                    case RE::WEAPON_TYPE::kTwoHandAxe:
                        return RE::BIPED_OBJECT::kTwoHandMelee;
                    case RE::WEAPON_TYPE::kStaff:
                        return RE::BIPED_OBJECT::kStaff;
                    default: continue;
                }
            }

            if (std::ranges::all_of(process->equippedObjects, [](RE::TESForm* form) { return form == nullptr; })) {
                return RE::BIPED_OBJECT::kTwoHandMelee;
            }
        }

        return RE::BIPED_OBJECT::kNone;
    };

    const auto bipedIndex = GetBipedIndex();
    if (bipedIndex == RE::BIPED_OBJECT::kNone) {
        return false;
    }

    const auto node = target->GetCurrentBiped()->objects[bipedIndex].partClone;
    return TryBlockInternal(attacker, target, node ? node->worldBound.center : RE::NiPoint3::Zero());
}

PRECISION_API::PreHitCallbackReturn BlockHandler::TryBlock(const PRECISION_API::PrecisionHitData& hitData)
{
    const auto attacker = hitData.attacker;
    const auto defender = hitData.target && hitData.target->IsPlayerRef() ? hitData.target->As<RE::Actor>() : nullptr;


    bool ret = attacker && defender && TryBlockInternal(attacker, defender, hitData.hitPos);
    return { .bIgnoreHit = ret };
}

void BlockHandler::Update()
{
    std::array<double,8> attackTimes;
    unsigned attacks=0;
    {std::lock_guard lock(attackEventLock);attacks=pendingAttackCount;
        attackTimes=pendingAttackTimes;pendingAttackCount=0;}
    if(!loggedEntry) {
        loggedEntry=true;
        logger::info("Player-time update hook entered: paused={}",Util::BlockSlowdownSuspended());
    }
    if (Util::BlockSlowdownSuspended()) {
        HellbladeTime::VFX::Publish({});
        return;
    }
    auto* player=RE::PlayerCharacter::GetSingleton();
    if (!player || player->IsDead() || player->IsInKillMove()) { Reset(); return; }
    RefreshAttackGraph(player);
    if(!loggedUpdate) {
        loggedUpdate=true;
        auto* vats=RE::VATS::GetSingleton();
        logger::info("Player-time update active: VATS mode={}, world={}, player={}, global={}",
            vats ? static_cast<unsigned>(vats->mode) : 0xffffffffu,vats ? vats->magicTimeSlowdown : -999.f,
            vats ? vats->playerMagicTimeSlowdown : -999.f,Util::GetGlobalTimeMult());
    }
    deltaTime = *Offsets::g_DeltaTime;
    deltaTimeRealTime = *Offsets::g_DeltaRealTime;

    const auto session=slowHelper.state();


    const float playerDelta=session.active ? deltaTimeRealTime : deltaTime;
    timeSpentBlocking += playerDelta;
    if (!player->IsBlocking() || (slowHelper.is_running() && session.source!=HellbladeTime::Source::Attack)) {
        timeSpentBlocking = 0.f;
    }

    if (timeCooldownRemaining > 0.f) {
        timeCooldownRemaining -= playerDelta;
    }

    for(unsigned i=0;i<attacks;++i) {
        auto* right=player->GetEquippedObject(false);
        auto* left=player->GetEquippedObject(true);
        const auto melee=[](RE::TESForm* form) {auto* weapon=form ? form->As<RE::TESObjectWEAP>() : nullptr;return weapon && weapon->IsMelee();};
        if((right || left) && !melee(right) && !melee(left)) continue;


        attackRandom+=0x9e3779b97f4a7c15ULL;
        auto random=attackRandom;
        random=(random^(random>>30))*0xbf58476d1ce4e5b9ULL;
        random=(random^(random>>27))*0x94d049bb133111ebULL;
        random^=random>>31;
        const float roll=float(random>>40)*(1.f/16777216.f);
        const bool selected=attackGate.Consider(attackTimes[i],roll,Settings::attack,slowHelper.is_running(),player->IsInCombat());
        if(loggedRolls++<8) logger::info("Attack slowdown roll: value={}, chance={}, enabled={}, busy={}, selected={}",
            roll,Settings::attack.chance,Settings::attack.enabled,slowHelper.is_running(),selected);
        if (selected) {
            if (slowHelper.start(Settings::attack.worldRate,Settings::attack.duration,HellbladeTime::Source::Attack))
                logger::info("Attack slowdown requested: {}s, world {}x/player 1x",Settings::attack.duration,Settings::attack.worldRate);
        }
    }
    slowHelper.update();
    const auto activeSession=slowHelper.state();
    if(activeSession.active && activeSession.serial!=loggedSession) {
        loggedSession=activeSession.serial;
        logger::info("Player-time session began: source={}, duration={} real seconds; player time 1x",
            activeSession.source==HellbladeTime::Source::Attack ? "attack" :
                activeSession.source==HellbladeTime::Source::PerfectBlock ? "perfect-block" : "timed-block",activeSession.duration);
    }
    HellbladeTime::VFX::Publish(activeSession);
}

void BlockHandler::Reset()
{
    timeSpentBlocking = 0.f;
    timeCooldownRemaining = 0.f;
    DetachAttackGraph();nextGraphRefresh=0;loggedUpdate=false;
    {std::lock_guard lock(attackEventLock);pendingAttackCount=0;swingSignals={};}
    attackGate={};
    slowHelper.stop();
    HellbladeTime::VFX::Reset();
}

void BlockHandler::RequestModAPIs()
{
    if (!API.Precision.g_ptr) {
        if (auto ptr = reinterpret_cast<PRECISION_API::IVPrecision3*>(PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V3))) {
            API.Precision.g_ptr = ptr;
            logger::info("Got Precision API"sv);
        }
    }

    if (!API.TDM.g_ptr) {
        if (auto ptr = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI())) {
            API.TDM.g_ptr = ptr;
            logger::info("Got TrueDirectionalMovement API"sv);
        }
    }
}

bool BlockHandler::TryRegisterPrecisionCallback()
{
    if (!API.Precision.g_ptr) {
        return false;
    }

    if (!API.Precision.isPreHitRegistered) {
        auto result = API.Precision.g_ptr->AddPreHitCallback(SKSE::GetPluginHandle(), [this](const auto& hitData) {
            return TryBlock(hitData);
        });

        switch (result) {
            case PRECISION_API::APIResult::OK:
                logger::info("Registered for Precision PreHitCallback"sv);
                API.Precision.isPreHitRegistered = true;
                break;
            case PRECISION_API::APIResult::AlreadyRegistered:
            case PRECISION_API::APIResult::NotRegistered:
                break;
        }
    }

    return API.Precision.isPreHitRegistered;
}

bool BlockHandler::TryBlockInternal(RE::Actor* attacker, RE::Actor* target, const RE::NiPoint3& hitPos)
{
    if (Util::DefinitelyGreater(timeCooldownRemaining, 0.f)) {
        return false;
    }

    if (timeSpentBlocking <= 0.f || !(Util::IsInBlockAngle(target, attacker) || API.TDM.IsActorTarget(attacker))) {
        return false;
    }








    float epsilon = 0.5f;
    if (const auto precision = API.Precision.g_ptr) {
        const float collisionLength = precision->GetAttackCollisionCapsuleLength(attacker->GetHandle(), PRECISION_API::RequestedAttackCollisionType::Current);
        if (Util::DefinitelyGreater(collisionLength, 0.f))
            epsilon = std::numeric_limits<float>::epsilon();
    }

    const bool isInPerfectBlockWindow = Settings::bPerfectBlockEnabled && Util::ApproximatelyGreaterOrEqual(Settings::fPerfectBlockWindow, timeSpentBlocking, epsilon);
    const bool isInTimedBlockWindow = isInPerfectBlockWindow || (Settings::bTimedBlockEnabled && Util::ApproximatelyGreaterOrEqual(Settings::fTimedBlockWindow, timeSpentBlocking, epsilon));

    if (isInTimedBlockWindow) {
        if (isInPerfectBlockWindow) {
            slowHelper.start(Settings::fPerfectBlockSlowdownMult,
                Settings::fPerfectBlockSlowdownDuration*Settings::blockDurationMultiplier,HellbladeTime::Source::PerfectBlock);
            Util::CastSpell(target, target, Forms::HB_PerfectBlockSpell);
        }

        if (!isInPerfectBlockWindow && Settings::bTimedBlockEnabled) {
            slowHelper.start(Settings::fTimedBlockSlowdownMult,
                Settings::fTimedBlockSlowdownDuration*Settings::blockDurationMultiplier,HellbladeTime::Source::TimedBlock);
        }

        Hit = true;


        if (auto module = GetModuleHandleW(L"Precision.dll")) {
            using NotifyBlock = void(__cdecl*)(std::uint32_t, float, float, float) noexcept;
            if (auto notify = reinterpret_cast<NotifyBlock>(GetProcAddress(module, "Redsand_NotifyPlayerBlock_v1"))) {
                notify(attacker->GetFormID(), hitPos.x, hitPos.y, hitPos.z);
            }
        }
        RE::HitData hitData;
        Util::HitData_Ctor(&hitData);
        Util::HitData_Populate(&hitData, attacker, target, attacker->GetAttackingWeapon());

        hitData.hitPosition = { hitPos.x, hitPos.y, hitPos.z };
        hitData.flags |= RE::HitData::Flag::kBlocked;

        bool isWeaponBlock = false;

        if (!Util::Actor_GetEquippedShield(target)) {
            isWeaponBlock = true;
            hitData.flags |= RE::HitData::Flag::kBlockWithWeapon;
        }

        SKSE::GetTaskInterface()->AddTask([target, isInPerfectBlockWindow, isWeaponBlock]() {
            Util::TryPlaySoundAt(target, isWeaponBlock ? Forms::HB_TimedBlock01SD : Forms::HB_TimedBlock02SD, 0.9f);
            if (isInPerfectBlockWindow)
                Util::TryPlaySoundAt(target, Forms::HB_PerfectBlockSD);
        });

        bool isLeftAttack = false;

        if (hitData.attackData) {
            isLeftAttack = hitData.attackData->IsLeftAttack();
            hitData.attackData->data.flags |= RE::AttackData::AttackFlag::kPowerAttack;
        }

        constexpr const std::string_view GRAPH_VAR_NO_STAGGER = "bNoStagger"sv;

        bool bNoStagger = false;
        attacker->GetGraphVariableBool(GRAPH_VAR_NO_STAGGER, bNoStagger);
        attacker->SetGraphVariableBool(GRAPH_VAR_NO_STAGGER, true);

        Util::PerformAction(Forms::ActionLargeRecoil, attacker);
        Util::PerformAction(Forms::ActionBlockHit, target);
        Util::Actor_DealDamage(attacker, target, nullptr, isLeftAttack);

        RE::PlayerControls::GetSingleton()->attackBlockHandler->GetRuntimeData().heldTimeMs = 0;

        attacker->SetGraphVariableBool(GRAPH_VAR_NO_STAGGER, bNoStagger);

        timeCooldownRemaining = Settings::fCooldownTime;
        Hit = false;

        return true;
    }

    return false;
}
