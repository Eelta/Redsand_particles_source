#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace HellbladeTime {
enum class Source : unsigned { Attack, TimedBlock, PerfectBlock };
enum class SwingSignal { EngineSwing, GraphSwing, CollisionStart, CollisionEnd };
inline std::optional<SwingSignal> ClassifySwing(std::string_view tag) noexcept {
    if(tag=="Collision_AttackStart" || tag=="Collision_Start") return SwingSignal::CollisionStart;
    if(tag=="Collision_AttackEnd" || tag=="Collision_Cancel" || tag=="attackStop" || tag=="staggerStart") return SwingSignal::CollisionEnd;
    if(tag=="weaponSwing" || tag=="weaponLeftSwing" || tag=="SoundPlay.WPNSwingUnarmed") return SwingSignal::GraphSwing;
    return {};
}



struct SwingSignals {
    bool explicitAttack=false;
    double lastEmission=-1000;
    bool Accept(SwingSignal signal,double now) noexcept {
        if(!std::isfinite(now)) return false;
        if(signal==SwingSignal::CollisionEnd) {explicitAttack=false;return false;}
        if(signal==SwingSignal::CollisionStart) explicitAttack=true;
        else if(explicitAttack) return false;
        if(now<lastEmission || now-lastEmission<.075) return false;
        lastEmission=now;return true;
    }
};
struct Session {
    bool active = false;
    float elapsed = 0, duration = 0;
    std::uint32_t serial = 0;
    Source source = Source::Attack;
};
inline float FiniteClamp(float value, float fallback, float lo, float hi) noexcept {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
struct AttackOptions {
    bool enabled = true, combatOnly = false;
    float chance = .10f, duration = 1.5f, worldRate = .08f, cooldown = 0.f;
    void Validate() noexcept {
        chance = FiniteClamp(chance, .10f, 0.f, 1.f);
        duration = FiniteClamp(duration, 1.5f, .1f, 15.f);
        worldRate = FiniteClamp(worldRate, .08f, .05f, 1.f);
        cooldown = FiniteClamp(cooldown, 0.f, 0.f, 60.f);
    }
};


struct AttackGate {
    double lastEvent = -1000, lastProc = -1000;
    bool Consider(double now, float roll, const AttackOptions& options,
        bool busy, bool inCombat) noexcept {
        if (!std::isfinite(now) || now < lastEvent || now - lastEvent < .075) return false;
        lastEvent = now;
        if (!options.enabled || busy || (options.combatOnly && !inCombat) ||
            now - lastProc < options.cooldown || !std::isfinite(roll) ||
            roll < 0 || roll >= options.chance || roll >= 1) return false;
        lastProc = now;
        return true;
    }
};
struct VisualOptions {
    bool enabled = true, onBlock = true, onAttack = true, randomColor = true;
    float brightness = 1.f, density = 1.f;
    float red = .12f, green = .58f, blue = 1.f;
    void Validate() noexcept {
        brightness = FiniteClamp(brightness, 1.f, 0.f, 8.f);
        density = FiniteClamp(density, 1.f, .25f, 3.f);
        red = FiniteClamp(red, .12f, 0.f, 1.f);
        green = FiniteClamp(green, .58f, 0.f, 1.f);
        blue = FiniteClamp(blue, 1.f, 0.f, 1.f);
    }
};
struct VisualFrame { Session session; VisualOptions options; };


struct SessionColors {
    std::uint64_t randomState=0;
    std::uint32_t serial=0;
    bool hasSession=false,hasPrevious=false;
    float red=0,green=0,blue=0;
    explicit SessionColors(std::uint64_t seed=0) : randomState(seed) {}
    float Next() noexcept {
        randomState+=0x9e3779b97f4a7c15ULL;
        auto value=randomState;
        value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
        value=(value^(value>>27))*0x94d049bb133111ebULL;
        value^=value>>31;
        return float(value>>40)*(1.f/16777216.f);
    }
    void ResetSession() noexcept {hasSession=false;}
    void Apply(const Session& session,VisualOptions& options) noexcept {
        if(!session.active || !options.randomColor) return;
        if(!hasSession || serial!=session.serial) {
            float r=0,g=0,b=0;
            bool chosen=false;
            for(unsigned attempt=0;attempt<32;++attempt) {
                r=Next();g=Next();b=Next();
                const float high=std::max({r,g,b});
                if(high<.25f) continue;
                r/=high;g/=high;b/=high;
                if(std::min({r,g,b})>.45f) continue;
                if(!hasPrevious || std::abs(r-red)+std::abs(g-green)+std::abs(b-blue)>.6f) {chosen=true;break;}
            }
            if(!chosen) {
                r=hasPrevious ? 1-red : .12f;
                g=hasPrevious ? 1-green : .58f;
                b=hasPrevious ? 1-blue : 1.f;
                const float high=std::max({r,g,b});r/=high;g/=high;b/=high;
            }
            red=r;green=g;blue=b;serial=session.serial;hasSession=hasPrevious=true;
        }
        options.red=red;options.green=green;options.blue=blue;
    }
};
}
