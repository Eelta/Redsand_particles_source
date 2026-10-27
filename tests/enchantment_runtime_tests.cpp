#include "../source/RedsandMeleeStyle.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace RE {
    enum class ActorValue { kNone, kCharge };
    namespace MagicSystem { enum class CastingSource { kLeftHand, kRightHand }; }
    struct NiColorA { float red, green, blue, alpha; };
}
namespace logger { template <class... T> void info(const char*, T&&...) {} }
namespace RedsandLog { using logger::info; }
struct Color { unsigned char red=0, green=0, blue=0; };
struct ShaderData {
    Color fillTextureEffectColorKey1, edgeEffectColor, fillTextureEffectColorKey2,
        fillTextureEffectColorKey3, colorKey1, colorKey2, colorKey3;
};
struct Shader { ShaderData data; };
struct Effect {
    struct Data { Shader* enchantShader = nullptr; } data;
    std::vector<std::string> keywords;
    bool HasKeywordString(std::string_view name) { return std::find(keywords.begin(), keywords.end(), name) != keywords.end(); }
    unsigned GetFormID() const { return 0x123; }
};
struct Item { Effect* baseEffect; };
struct Magic {
    std::vector<Item*> effects;
    Item* dominant = nullptr;
    float cost = 163.f;
    Item* GetCostliestEffectItem() { return dominant; }
    unsigned GetFormID() const { return 0xff123; }
    template <class T> float CalculateMagickaCost(T*) { return cost; }
};
struct Actor {
    float charge = 3000.f;
    Actor* AsActorValueOwner() { return this; }
    float GetActorValue(RE::ActorValue) { return charge; }
    unsigned GetFormID() const { return 0x14; }
};
struct Weapon {
    std::optional<double> charge = 100.; Magic* magic = nullptr; Magic* poison = nullptr;
    std::optional<double> GetEnchantmentCharge() { return charge; }
    unsigned GetFormID() const { return 0xfe123; }
};
struct Override { std::optional<RE::NiColorA> baseColorOverride; float lifetime = 7.f; };
struct TrailDefinition { Override trailOverride; };
Magic* GetPoison(Weapon* weapon) { return weapon->poison; }
Magic* GetEnchantment(Weapon* weapon) { return weapon->magic; }
RE::ActorValue GetActorValueForCost(Magic*, RE::MagicSystem::CastingSource) { return RE::ActorValue::kCharge; }
namespace RedsandEnchantmentProfiles { std::string Key(Magic* magic) { return magic ? "fire" : "none"; } }
struct Result { std::optional<RE::NiColorA> color; std::string source; float suppliedLifetime; RedsandSettings::Config config; std::string key, fallback; };
struct Runtime {
    std::optional<RE::NiColorA> ownRule;
    std::optional<RE::NiColorA> compatibleRule;
    RedsandSettings::Config config;
    unsigned ruleQueries = 0;
    unsigned compatibleQueries = 0;
    bool GetTrailDefinition(int, Weapon*, bool, TrailDefinition& rule, bool ordinary = false) {
        if (ordinary) { ++compatibleQueries; rule.trailOverride.baseColorOverride = compatibleRule; return compatibleRule.has_value(); }
        ++ruleQueries; rule.trailOverride.baseColorOverride = ownRule; return ownRule.has_value();
    }
    Result Resolve(Weapon* a_weaponItem, std::optional<Override> a_trailOverride = {}, bool companion = false, float charge = 3000.f) {
        bool redsandLegacyTrail = companion, redsandNativeOnly = companion, a_bIsLeftHand = false;
        int a_actorHandle = 0;
        auto redsandActor = std::make_shared<Actor>(); redsandActor->charge = charge;
        Weapon* redsandWeapon = a_weaponItem;
        RedsandSettings::Config redsandConfig = config;
        std::string redsandEnchantmentKey = !a_weaponItem || (!a_weaponItem->magic && !a_weaponItem->poison) || (charge < 163.f && !a_weaponItem->poison) ? "none" : "fire", redsandEnchantmentFallback;
        RedsandSettings::ApplyMeleeAppearance(redsandConfig, redsandEnchantmentKey, "");
        auto baseColorOverride = a_trailOverride ? a_trailOverride->baseColorOverride : std::optional<RE::NiColorA>{};
#include "../source/TrailEnchantment.inc"
        return {baseColorOverride, std::string(redsandColorSource), a_trailOverride ? a_trailOverride->lifetime : 1.f, redsandConfig, redsandEnchantmentKey, redsandEnchantmentFallback};
    }
};
void Check(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
int main()
{
    try {
        Shader gray, green;
        gray.data.fillTextureEffectColorKey1 = {31,31,31};
        green.data.colorKey3 = {25,255,80};
        Effect neutral{{&gray}}, colorful{{&green}}, empty{};
        Item a{&neutral}, b{&colorful}, c{&empty};
        Magic magic{{&a,&b,&c}, &a}; Weapon weapon; weapon.magic = &magic;
        Runtime runtime;
        auto result = runtime.Resolve(&weapon);
        Check(result.color && result.color->green == 1.f && result.color->red == 25.f/255.f,
            "production fallback lost the green effect behind a neutral dominant effect");
        runtime.ownRule = RE::NiColorA{.32f,1.f,.12f,1.f};
        result = runtime.Resolve(&weapon, Override{});
        Check(result.color && result.color->red == .32f && result.source == "particle-rule-with-api-mesh" &&
            result.suppliedLifetime == 7.f && runtime.ruleQueries == 1,
            "API mesh/lifetime override skipped the owned palette or overwrote its supplied lifetime");
        result = runtime.Resolve(&weapon, Override{RE::NiColorA{1,1,1,1}});
        Check(result.color && result.color->red == 1.f && result.color->blue == 1.f && runtime.ruleQueries == 1,
            "explicit API white was replaced by an automatic hue");
        result = runtime.Resolve(&weapon, Override{}, false, 41.f);
        Check(result.color && result.color->red == .025f && result.color->blue == .65f && runtime.ruleQueries == 1,
            "depleted charge was recolored by a stale green palette rule");
        result = runtime.Resolve(&weapon, {}, true);
        Check(!result.color && runtime.ruleQueries == 1, "companion ribbon entered particle fallback");
        runtime.ownRule.reset(); colorful.keywords = {"MagicDamageShock"};
        result = runtime.Resolve(&weapon);
        Check(result.color && result.color->red == .48f && result.color->blue == 1.f,
            "direct keyword matching failed without a global editor-ID registry");
        magic.effects = {&a,&c}; magic.dominant = &a;
        result = runtime.Resolve(&weapon);
        Check(!result.color && result.source == "original-blue-default", "all-neutral shaders invented a white fallback");



        Effect purple{{&green}}; green.data.colorKey3 = {62,49,221};
        Item fourth{&purple}; magic.effects = {&a,&b,&c,&fourth}; magic.dominant = &fourth;
        colorful.keywords.clear(); runtime.compatibleRule = RE::NiColorA{.1f,.6f,.1f,1.f};
        result = runtime.Resolve(&weapon);
        Check(result.color && result.color->red == .1f && result.color->green == .6f &&
            result.source == "compatible-enchantment-color-rule",
            "raw purple shader keys overrode the installed green enchantment rule");
        auto supplied = Override{}; supplied.lifetime = 9.f;
        result = runtime.Resolve(&weapon, supplied);
        Check(result.color && result.color->green == .6f && result.suppliedLifetime == 9.f,
            "compatibility color altered the API lifetime or was skipped by a mesh override");
        runtime.ownRule = RE::NiColorA{1.f,.3f,.055f,1.f};
        result = runtime.Resolve(&weapon, supplied);
        Check(result.color && result.color->red == 1.f && result.color->green == .3f,
            "compatibility fallback overrode Redsand's explicit palette rule");
        result = runtime.Resolve(&weapon, Override{RE::NiColorA{1,1,1,1}});
        Check(result.color && result.color->green == 1.f && result.color->blue == 1.f, "compatibility rule replaced explicit API white");
        result = runtime.Resolve(&weapon, supplied, false, 41.f);
        Check(result.color && result.color->blue == .65f && result.color->green == .08f, "compatibility green survived charge depletion");
        const auto queries = runtime.compatibleQueries;
        result = runtime.Resolve(&weapon, supplied, true);
        Check(!result.color && runtime.compatibleQueries == queries, "particle fallback changed or queried companion ribbon color");
        runtime.ownRule.reset(); runtime.compatibleRule.reset();
        RedsandSettings::EnchantmentAppearance appearance;
        appearance.enabled = true; appearance.color.custom = true; appearance.color.rgb = {1.f, .1f, .2f}; appearance.brightness = 3.f; appearance.density = 0.f;
        runtime.config.enchantments.Set("fire", appearance);
        result = runtime.Resolve(&weapon);
        Check(result.config.swordBrightness == 3.f && result.config.swordDensity == 0.f && result.config.colors[0].rgb[0] == 1.f,
            "exact enchantment profile did not reach production melee settings");
        runtime.config.enchantments.Set("unknown", appearance);
        appearance.brightness = 2.f; runtime.config.enchantments.Set("none", appearance);
        result = runtime.Resolve(&weapon, {}, false, 0.f);
        Check(result.config.swordBrightness == 2.f && result.key == "none", "depleted profile mixed with active enchantment");
        result = runtime.Resolve(&weapon, {}, true);
        Check(!result.color, "profile recolored companion ribbon");
        weapon.magic = nullptr;
        appearance.brightness = 4.f; runtime.config.enchantments.Set("none", appearance);
        result = runtime.Resolve(&weapon);
        Check(result.config.swordBrightness == 4.f && result.key == "none", "unenchanted fallback was ignored");
        std::cout << "Actual TrailEnchantment fragment: API palette fill, three effects, neutral masks, direct keywords, explicit white, depletion and companion isolation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
