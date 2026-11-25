#include "../source/RedsandSettings.h"
#include "../source/RedsandEnchantmentProfiles.h"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(bool ok, const char* text) { if (!ok) { throw std::runtime_error(text); } }
int main() try
{
	using namespace RedsandSettings;
	const auto path = std::filesystem::temp_directory_path() / ("redsand-profiles-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
	Load(path);
	auto config = Get();
	Check(config.impactDensity == .1f,
		"performance defaults lost");
	Check(!config.slashRandomColor && !config.slashRandomValues, "random options defaulted on");
	EnchantmentAppearance fire, unknown, none;
	fire.enabled = unknown.enabled = none.enabled = true;
    fire.color.custom = true;
    fire.swordReleasedTrail=true;fire.swordRibbonLifetime=.065f;fire.swordParticleLifetime=.13f;fire.swordRibbonWidth=.21f;fire.swordTrailMomentum=.2f;
    fire.swordRibbonFadeOut=.31f;fire.swordParticleFadeOut=.45f;fire.swordParticleGravity=.65f;
    fire.swordEnabled = true; fire.swordPlayer = false; fire.swordNPC = true; fire.swordTip = false; fire.swordKeepPrecisionTrail = false;
    fire.bowEnabled = true; fire.bowPlayer = true; fire.bowNPC = false; fire.bowGathering = false; fire.bowRings = false; fire.bowTrails = true;
    fire.bowBrightness = 6.f; fire.bowDensity = .13f; fire.bowSize = 2.f; fire.bowColor = {true, {.1f,.9f,.3f}};
    config.swordEnabled = false; config.swordBrightness = 0.f; config.swordDensity = 0.f;
    config.bowEnabled = false; config.bowBrightness = 0.f; config.bowDensity = 0.f;
	fire.brightness = 2.75f; fire.density = 0.f; fire.size = .35f; fire.spread = .17f; fire.color.rgb = { .8f, .2f, .1f };
	unknown.brightness = 4.f; none.brightness = 5.f;
	config.enchantments.Set("fire", fire);
	const auto snapshot = config;
	config.enchantments.Set("unknown", unknown);
	config.enchantments.Set("none", none);
	Check(!snapshot.enchantments.Find("unknown"), "editing a profile mutated an active settings snapshot");
	Set(config); FlushPendingSave(); Load(path);
	Check(Get() == config, "profiles changed after saving and reloading");
    auto inherited = Get();
    auto disabled = fire;
    disabled.enabled = false;
    inherited.enchantments.Set("fire", disabled);
    ApplyMeleeAppearance(inherited, "fire", "");
    ApplyBowAppearance(inherited, "fire");
    Check(!inherited.swordEnabled && inherited.swordBrightness == 0.f && !inherited.bowEnabled && inherited.bowDensity == 0.f,
        "a disabled category overrode the homepage");
    inherited.enchantments.Set("fire", fire);
    ApplyMeleeAppearance(inherited, "fire", "");
    ApplyBowAppearance(inherited, "fire");
    Check(inherited.swordEnabled && inherited.swordReleasedTrail && inherited.swordBrightness == fire.brightness &&
        inherited.bowEnabled && inherited.bowDensity == fire.bowDensity, "reenabling the category did not restore its saved values");
	fire.enabled = true; fire.brightness = std::numeric_limits<float>::quiet_NaN(); fire.density = -1.f; fire.size = 100.f;
	config.enchantments.Set("fire", fire); Set(config);
	const auto sanitized = Get(); const auto* value = sanitized.enchantments.Find("fire");
	Check(value->brightness == 1.f && value->density == 0.f && value->size == maxSize, "invalid profile values were not clamped");
	ResetDefaults(); FlushPendingSave(); Load(path);
	Check(!Get().enchantments.Find("fire") && !Get().slashRandomValues,
		"restore defaults kept profiles or expensive/random slash defaults");
    using namespace RedsandEnchantmentProfiles;
    Facts facts;
    Check(categoryKeys.size() == 11 && Select(facts) == Category::None, "fixed category list or unenchanted fallback changed");
    facts.hasMagic = true; Check(Select(facts) == Category::Unknown, "unknown category missing");
    facts.matches = Bit(Category::Fire) | Bit(Category::Frost) | Bit(Category::Shock);
    Check(Select(facts) == Category::Fire, "multi-element palette priority changed");
    facts.matches |= Bit(Category::Green); Check(Select(facts) == Category::Green, "green shader no longer precedes fire");
    facts.sun = true; Check(Select(facts) == Category::Sun, "sun palette priority changed");
    facts.bound = true; Check(Select(facts) == Category::Bound, "bound palette no longer precedes any-match rules");
    facts.depleted = true; Check(Select(facts) == Category::None, "depleted category did not merge with unenchanted");
    facts = {}; facts.hasMagic = facts.poison = true; Check(Select(facts) == Category::Green, "poison default is no longer green");
    facts.matches = Bit(Category::Purple); Check(Select(facts) == Category::Purple, "poison was forcibly recolored green before existing rules");
	std::filesystem::remove(path);
	std::cout << "Independent profiles, immutable snapshots, persistence, fallback priority and performance defaults passed\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
