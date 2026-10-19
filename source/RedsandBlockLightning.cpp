#include "RedsandBlockLightning.h"
#include "RedsandBlockLightningPolicy.h"
#include "RedsandSettings.h"
#include <RE/T/TESHitEvent.h>
#include <RE/S/ScriptEventSourceHolder.h>
#include <chrono>
#include <atomic>
#include <mutex>
#include <random>

namespace RedsandBlockLightning
{
namespace
{
    using Clock = std::chrono::steady_clock;
    struct Request { RE::FormID cause; RE::NiPoint3 point; double time; };
    std::mutex requestsLock;
    std::array<Request, 16> requests{};
    unsigned requestCount = 0;
    std::atomic_bool pending{false};
    Gate gate;
    std::mt19937 random{static_cast<unsigned>(Clock::now().time_since_epoch().count())};
    float Roll() { return std::generate_canonical<float, 24>(random); }
    double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
    struct Branch
    {
        RE::NiNode* node = nullptr;
        RE::BSEffectShaderProperty* shader = nullptr;
        RE::BSGeometry* geometry = nullptr;
        RE::NiTransform original{};
    };
    struct Effect
    {
        RE::NiPointer<RE::BSTempEffectParticle> particle;
        RE::TESObjectCELL* cell = nullptr;
        std::array<Branch, pieceCount> branches{};
        std::array<float, 3> color{};
        RE::NiMatrix3 rotation{};
        float elapsed = 0, wait = 0;
        bool bound = false;
    };
    std::array<Effect, 3> effects{};
    unsigned logged = 0;
    void Retire(Effect& effect)
    {
        if (effect.particle) {
            if (auto root = effect.particle->particleObject) root->SetAppCulled(true);
            effect.particle->age = effect.particle->lifetime+1.f;
        }
        effect = {};
    }
    void Queue(RE::FormID cause, const RE::NiPoint3& point)
    {
        const auto config = RedsandSettings::Get();
        if (!config.impactEnabled || !config.blockLightning) return;
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
        std::lock_guard lock(requestsLock);
        if (requestCount < requests.size()) { requests[requestCount++] = {cause, point, Now()}; pending.store(true, std::memory_order_release); }
    }
    class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
    {
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
        {
            if (event && event->target && event->target->IsPlayerRef() && event->flags.any(RE::TESHitEvent::Flag::kHitBlocked) &&
                !event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) {

                auto position = event->target->GetPosition(); position.z += 95.f;
                Queue(event->cause ? event->cause->GetFormID() : 0, position);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    } hitSink;
    bool Bind(Effect& effect)
    {
        auto* root = effect.particle->particleObject.get();
        if (!root || root->name != "RedsandBlockLightning_v3"sv) return false;
        for (unsigned i = 0; i < pieceCount; ++i) {
            auto* object = root->GetObjectByName(RE::BSFixedString(fmt::format("RSBL_P{:03}", i)));
            auto* shape = root->GetObjectByName(RE::BSFixedString(fmt::format("RSBL_G{:03}", i)));
            auto* node = object ? object->AsNode() : nullptr;
            auto* geometry = shape ? netimmerse_cast<RE::BSGeometry*>(shape) : nullptr;
            auto* shader = geometry ? netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get()) : nullptr;
            auto* material = shader ? shader->GetMaterial() : nullptr;
            if (!node || !material) return false;

            auto* copy = static_cast<RE::BSEffectShaderMaterial*>(material->Create());
            if (!copy) return false;
            copy->CopyMembers(material);
            copy->baseColor = RE::NiColorA{effect.color[0], effect.color[1], effect.color[2], 0.f};
            shader->SetMaterial(copy, true);
            copy->~BSEffectShaderMaterial(); RE::free(copy);
            effect.branches[i] = {node, shader, geometry, node->local};
        }
        effect.bound = true;
        return true;
    }
    void Spawn(const Request& request, RE::PlayerCharacter& player, const RedsandSettings::Config& config)
    {

        auto slot = std::find_if(effects.begin(), effects.end(), [](auto& e) { return !e.particle; });
        if (slot == effects.end()) return;
        auto* cell = player.GetParentCell(); if (!cell) return;
        auto point = request.point;
        if ((point-player.GetPosition()).Length() > 220.f) { point = player.GetPosition(); point.z += 95.f; }
        RE::NiMatrix3 rotation; rotation.SetEulerAnglesXYZ(0.f, 0.f, Roll()*6.2831853f);
        const auto path = fmt::format("RedsandBlockLightning/Arc{}.nif", random()%4);
        auto* particle = RE::BSTempEffectParticle::Spawn(cell, 8.f, path.c_str(), rotation, point, 1.f, 7, nullptr);
        if (!particle) { if (logged++ < 8) logger::warn("Block lightning: model spawn failed"); return; }
        slot->particle.reset(particle); slot->cell = cell; slot->rotation = rotation;
        slot->color = config.blockLightningColor;
        if (config.blockLightningRandomColor) {

            std::array<float, 3> c{Roll(), Roll(), Roll()};
            auto [lo, hi] = std::minmax_element(c.begin(), c.end()); const float minimum = *lo, range = *hi-minimum;
            if (range > .001f) { for (auto& x : c) x = .035f+.965f*(x-minimum)/range; slot->color = c; }
        }
        if (logged++ < 8) logger::info("Block lightning spawned: attacker={:X} world-space RGB={}/{}/{}; visual only", request.cause, slot->color[0], slot->color[1], slot->color[2]);
    }
}
void Notify(RE::TESObjectREFR* target, RE::TESObjectREFR* cause, const RE::NiPoint3& point)
{
    if (target && target->IsPlayerRef()) Queue(cause ? cause->GetFormID() : 0, point);
}
void Initialize()
{
    if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
        events->AddEventSink<RE::TESHitEvent>(&hitSink);
        logger::info("Block lightning: player blocked-hit observer registered; no time or combat hooks");
    }
}
void Reset()
{
    { std::lock_guard lock(requestsLock); requestCount = 0; pending.store(false, std::memory_order_release); }
    for (auto& effect : effects) Retire(effect);
    gate.Reset();
}
void Update()
{
    static auto previous = Clock::now();
    const auto now = Clock::now();
    const float realDelta = std::clamp(std::chrono::duration<float>(now-previous).count(), 0.f, .1f); previous = now;
    const auto config = RedsandSettings::Get();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    if (!player || !player->GetParentCell() || player->IsDead() || !config.impactEnabled || !config.blockLightning) { Reset(); return; }
    const bool paused = !ui || ui->GameIsPaused() || ui->IsMenuOpen("Loading Menu") || ui->IsMenuOpen("Main Menu");
    std::array<Request, 16> batch{}; unsigned count = 0;
    if (pending.load(std::memory_order_acquire)) {
        std::lock_guard lock(requestsLock); count = requestCount; std::copy_n(requests.begin(), count, batch.begin());
        requestCount = 0; pending.store(false, std::memory_order_release);
    }
    if (!paused) for (unsigned i = 0; i < count; ++i) {
        const auto& request = batch[i];
        if (Now()-request.time < .5 && gate.Accept(request.cause, request.time) &&
            Selected(true, config.blockLightningRandomTrigger, config.blockLightningChance, Roll())) Spawn(request, *player, config);
    }
    const auto delta = paused ? 0.f : realDelta;
    for (auto& effect : effects) {
        if (!effect.particle) continue;
        if (effect.cell != player->GetParentCell()) { Retire(effect); continue; }
        effect.particle->lifetime = std::max(effect.particle->lifetime, effect.particle->age+3.f);
        if (!effect.bound) {
            effect.wait += delta;
            if (!Bind(effect)) { if (effect.wait > 2.f) { logger::warn("Block lightning: async scene unavailable"); Retire(effect); } continue; }
        }
        effect.elapsed += delta;
        if (effect.elapsed >= duration) { Retire(effect); continue; }
        auto* root = effect.particle->particleObject.get();
        root->local.scale = 1.2f*config.blockLightningSize;
        for (unsigned i = 0; i < pieceCount; ++i) {
            auto& branch = effect.branches[i]; const auto pose = Animate(i, effect.elapsed);
            branch.node->local.scale = branch.original.scale;
            branch.geometry->SetAppCulled(!Visible(i, pose));
            branch.shader->SetMaterialAlpha(pose.alpha);
            auto* material = branch.shader->GetMaterial();
            material->baseColorScale = 14.f*config.blockLightningBrightness;
            material->texCoordScale[0] = material->texCoordScale[1] = RE::NiPoint2{MaskScale(pose), 1.f};
            material->texCoordOffset[0] = material->texCoordOffset[1] = RE::NiPoint2{MaskOffset(pose), 0.f};
            branch.geometry->SetMaterialNeedsUpdate(true);
        }
        RE::NiUpdateData update{}; root->Update(update);
    }
}
}



extern "C" __declspec(dllexport) void __cdecl Redsand_NotifyPlayerBlock_v1(std::uint32_t cause, float x, float y, float z) noexcept
{
    try { RedsandBlockLightning::Queue(cause, RE::NiPoint3{x, y, z}); }
    catch (...) {   }
}
