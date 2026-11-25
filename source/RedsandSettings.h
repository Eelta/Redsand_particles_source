#pragma once

#include <filesystem>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace RedsandSettings
{
	enum class Language { English = 0, Chinese = 1 };

	inline constexpr float maxBrightness = 20.0f;
	inline constexpr float maxDensity = 10.0f;
	inline constexpr float maxRedsandDotDensity = maxDensity / .65f;
	inline constexpr float defaultSpread = 1.0f;
	inline constexpr float maxSpread = 10.0f;
	inline constexpr float maxSwingParticleWidth = 10.f;
	inline constexpr float defaultSwingSpeed = 1.f, maxSwingSpeed = 2.f;
	inline constexpr float defaultRibbonWidth = .15f, maxRibbonWidthMultiplier = 2.f;
	inline constexpr float minSwingDuration = .18f, maxSwingDuration = 2.7f;
	inline constexpr float minSize = 0.05f;
	inline constexpr float maxSize = 10.0f;
	inline constexpr float minCutLength = 0.05f;
	inline constexpr float maxCutLength = 20.f;
	inline constexpr float minCutWidth = 0.1f;
	inline constexpr float maxCutWidth = 4.f;
    inline constexpr std::array<float, 3> defaultLightningColor{.12f, .58f, 1.f};
	inline constexpr float impactBrightnessBaseline = 10.f;
	inline constexpr float impactDensityBaseline = 10.f;
	inline constexpr float cutLengthBaseline = 10.f;
	inline constexpr std::array<float, 4> impactBrightnessLevels{ 0.10f, 0.50f, 1.0f, 12.0f };
	inline constexpr std::array<float, 4> impactDensityLevels{ 0.10f, 0.50f, 1.0f, 6.0f };
	inline constexpr std::array<float, 4> impactSizeLevels{ 0.10f, 0.50f, 1.0f, 6.0f };

	enum class ColorTarget : unsigned { Melee, Bow, Hit, Cut, Block, Count };
	struct EffectColor
	{
		bool custom = true;
		std::array<float, 3> rgb{ 1.f, 1.f, 1.f };
		bool operator==(const EffectColor&) const = default;
	};
	inline constexpr std::array<const char*, 5> colorNames{ "Melee", "Bow", "Hit", "Cut", "Block" };
	inline constexpr std::array<EffectColor, 5> defaultColors{
		EffectColor{ false, { .0384615f, .123077f, 1.f } }, EffectColor{ false, { .0384615f, .123077f, 1.f } },
		EffectColor{ true, { 1.f, .72f, .24f } },
		EffectColor{ true, { .835886f, .537747f, 0.f } }, EffectColor{ true, { 1.f, .72f, .24f } }
	};
	struct RedsandTrailStyle
	{
		float arcBrightness = 1.f, particleBrightness = 1.f;
		float density = 1.f, size = 1.f;
		bool player = true, npc = true;
		EffectColor color = defaultColors[0];
		bool operator==(const RedsandTrailStyle&) const = default;
	};
	struct EnchantmentAppearance
	{
		bool enabled = false;
		EffectColor color{ false, { .0384615f, .123077f, 1.f } };
		bool swordReleasedTrail = false;
		RedsandTrailStyle redsand;
		float swordRibbonLifetime = .2f, swordParticleLifetime = .3f, swordRibbonWidth = defaultRibbonWidth, swordTrailMomentum = defaultSwingSpeed;
		float swordRibbonFadeOut = .25f, swordParticleFadeOut = .4f, swordParticleGravity = .3f;
		float swordParticleWidth = 0.f;
		bool swordEnabled = true, swordPlayer = true, swordNPC = true, swordTip = true, swordKeepPrecisionTrail = true;
		bool bowEnabled = true, bowPlayer = true, bowNPC = true, bowGathering = true, bowRings = true, bowTrails = true;
		EffectColor bowColor{ false, { .0384615f, .123077f, 1.f } };
		float bowBrightness = 1.f, bowDensity = 1.f, bowSize = 1.f;
		float brightness = 1.f, density = 1.f, size = 1.f, spread = 1.f;
		bool operator==(const EnchantmentAppearance&) const = default;
	};
	struct EnchantmentProfiles
	{
		using Map = std::map<std::string, EnchantmentAppearance, std::less<>>;
		std::shared_ptr<const Map> values;
		const EnchantmentAppearance* Find(std::string_view key) const
		{
			if (!values) { return nullptr; }
			const auto it = values->find(key);
			return it == values->end() ? nullptr : &it->second;
		}
		void Set(std::string key, EnchantmentAppearance appearance)
		{
			auto next = values ? std::make_shared<Map>(*values) : std::make_shared<Map>();
			(*next)[std::move(key)] = appearance;
			values = std::move(next);
		}
		bool operator==(const EnchantmentProfiles& other) const
		{
			return values == other.values || ((!values || values->empty()) && (!other.values || other.values->empty())) ||
				(values && other.values && *values == *other.values);
		}
	};

	struct Config
	{
		Language language = Language::English;
		bool diagnostics = false;
		bool swordReleasedTrail = false;
		RedsandTrailStyle redsand;
		float swordRibbonLifetime = .2f, swordParticleLifetime = .3f, swordRibbonWidth = defaultRibbonWidth, swordTrailMomentum = defaultSwingSpeed;
		float swordRibbonFadeOut = .25f, swordParticleFadeOut = .4f, swordParticleGravity = .3f;
		float swordParticleWidth = 0.f;
		bool swordEnabled = true;
		bool swordPlayer = true;
		bool swordNPC = true;
		bool swordTip = true;
		bool swordKeepPrecisionTrail = true;
		float swordBrightness = 1.0f;
		float swordDensity = 1.0f;
		float swordSpread = defaultSpread;
		float swordSize = 1.0f;
		bool bowEnabled = true;
		bool bowPlayer = true;
		bool bowNPC = true;
		float bowBrightness = 1.0f;
		float bowDensity = 1.0f;
		bool bowGathering = true;
		bool bowRings = true;
		bool bowTrails = true;
		bool impactEnabled = true;
		bool impactHit = true;
		bool impactSlash = true;
		bool slashRandomColor = false;
		bool slashRandomValues = false;
		float slashRandomRange = .5f;
		bool impactBlock = true;
        bool blockLightning = false;
        bool blockLightningRandomTrigger = true;
        bool blockLightningRandomColor = false;
        float blockLightningChance = .1f;
        float blockLightningBrightness = 1.f;
        float blockLightningSize = 1.f;
        std::array<float, 3> blockLightningColor = defaultLightningColor;
		float impactBrightness = 1.f;
		float impactDensity = .1f;
		float impactSize = 1.f;
		float hitBrightness = 1.f;
		float hitDensity = .1f;
		float hitSize = 1.f;
		float cutBrightness = 1.f;
		float cutLength = 1.f;
		float cutWidth = 1.f;
		std::array<EffectColor, 5> colors = defaultColors;
		EnchantmentProfiles enchantments;

		bool operator==(const Config&) const = default;
	};
	struct SwingTiming
	{
		float arcHold, arcFade, particleHold, particleFade;

		static SwingTiming FromDuration(float seconds)
		{
			const float part = std::clamp(seconds, minSwingDuration, maxSwingDuration) / 9.f;
			return {2.f * part, 3.f * part, 4.f * part, 5.f * part};
		}
	};
	template <class Settings>
	float SwingDuration(const Settings& settings)
	{
		return std::clamp(settings.swordParticleLifetime + settings.swordParticleFadeOut,
			minSwingDuration, maxSwingDuration);
	}
	template <class Settings>
	void SetSwingDuration(Settings& settings, float seconds)
	{
		const auto timing = SwingTiming::FromDuration(seconds);
		settings.swordRibbonLifetime = timing.arcHold;
		settings.swordRibbonFadeOut = timing.arcFade;
		settings.swordParticleLifetime = timing.particleHold;
		settings.swordParticleFadeOut = timing.particleFade;
	}

	inline const EnchantmentAppearance* AppearanceFor(const Config& config, std::string_view key, std::string_view fallback)
	{
		const auto* exact = config.enchantments.Find(key);
		if (exact && exact->enabled) { return exact; }
		const auto* group = config.enchantments.Find(fallback);
		return group && group->enabled ? group : nullptr;
	}
	inline void ApplyMeleeAppearance(Config& config, std::string_view key, std::string_view fallback)
	{
		if (const auto* appearance = AppearanceFor(config, key, fallback)) {
			config.swordReleasedTrail = appearance->swordReleasedTrail;
			config.redsand = appearance->redsand;
			config.swordRibbonLifetime = appearance->swordRibbonLifetime; config.swordParticleLifetime = appearance->swordParticleLifetime;
			config.swordRibbonFadeOut = appearance->swordRibbonFadeOut; config.swordParticleFadeOut = appearance->swordParticleFadeOut;
			config.swordParticleGravity = appearance->swordParticleGravity;
			config.swordParticleWidth = appearance->swordParticleWidth;
			config.swordRibbonWidth = appearance->swordRibbonWidth; config.swordTrailMomentum = appearance->swordTrailMomentum;
			config.swordEnabled = appearance->swordEnabled;
			config.swordPlayer = appearance->swordPlayer; config.swordNPC = appearance->swordNPC;
			config.swordTip = appearance->swordTip; config.swordKeepPrecisionTrail = appearance->swordKeepPrecisionTrail;
			config.colors[static_cast<unsigned>(ColorTarget::Melee)] = appearance->color;
			config.swordBrightness = appearance->brightness;
			config.swordDensity = appearance->density;
			config.swordSize = appearance->size;
			config.swordSpread = appearance->spread;
		}
	}

	inline void ApplyBowAppearance(Config& config, std::string_view key)
	{
		if (const auto* appearance = AppearanceFor(config, key, "")) {
			config.bowEnabled = appearance->bowEnabled; config.bowPlayer = appearance->bowPlayer; config.bowNPC = appearance->bowNPC;
			config.bowGathering = appearance->bowGathering; config.bowRings = appearance->bowRings; config.bowTrails = appearance->bowTrails;
			config.bowBrightness = appearance->bowBrightness; config.bowDensity = appearance->bowDensity;
			config.colors[static_cast<unsigned>(ColorTarget::Bow)] = appearance->bowColor;
		}
	}

	[[nodiscard]] Config Get();
	void Set(Config config);
	void ResetDefaults();
	void Load(const std::filesystem::path& path = "Data/SKSE/Plugins/RedsandParticles.ini");

	void TickPendingSave();
	void FlushPendingSave();
	[[nodiscard]] bool SaveFailed();
}
