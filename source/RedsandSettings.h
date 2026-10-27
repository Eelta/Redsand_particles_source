#pragma once

#include <filesystem>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace RedsandSettings
{
	enum class Language { English = 0, Chinese = 1 };
	enum class BlockStyle : unsigned { NormalBlock, TimedSparks, TimedFlare, PerfectRing, SimpleTimed, StarRing, Count };

	inline constexpr float maxBrightness = 20.0f;
	inline constexpr float maxDensity = 10.0f;
	inline constexpr float defaultSpread = 1.0f;
	inline constexpr float maxSpread = 10.0f;
	inline constexpr float minSize = 0.05f;
	inline constexpr float maxSize = 10.0f;
	inline constexpr float minCutLength = 0.05f;
	inline constexpr float maxCutLength = 20.f;
	inline constexpr float defaultSlashDistance = .1f;
    inline constexpr std::array<float, 3> defaultLightningColor{.12f, .58f, 1.f};
	inline constexpr float impactBrightnessBaseline = 10.f;
	inline constexpr float impactDensityBaseline = 10.f;
	inline constexpr float slashDensityBaseline = 10.f;
	inline constexpr float maxSlashDensity = 1.f;
	inline constexpr float cutLengthBaseline = 10.f;
	inline constexpr float slashDistanceBaseline = 20.f;
	inline constexpr float slashSpreadBaseline = .13f;
	inline constexpr float maxSlashSpread = 5.f;
	inline constexpr float minSlashDistance = .1f;
	inline constexpr float maxSlashDistance = 20.f;
	inline constexpr std::array<float, 4> impactBrightnessLevels{ 0.10f, 0.50f, 1.0f, 12.0f };
	inline constexpr std::array<float, 4> impactDensityLevels{ 0.10f, 0.50f, 1.0f, 6.0f };
	inline constexpr std::array<float, 4> impactSizeLevels{ 0.10f, 0.50f, 1.0f, 6.0f };

	enum class ColorTarget : unsigned { Melee, Bow, Hit, SlashSparks, Cut, Block, Wave, Count };
	struct EffectColor
	{
		bool custom = true;
		std::array<float, 3> rgb{ 1.f, 1.f, 1.f };
		bool operator==(const EffectColor&) const = default;
	};
	inline constexpr std::array<const char*, 7> colorNames{ "Melee", "Bow", "Hit", "SlashSparks", "Cut", "Block", "Wave" };
	inline constexpr std::array<EffectColor, 7> defaultColors{
		EffectColor{ false, { .0384615f, .123077f, 1.f } }, EffectColor{ false, { .0384615f, .123077f, 1.f } },
		EffectColor{ true, { 1.f, .72f, .24f } }, EffectColor{ true, { 1.f, .72f, .24f } },
		EffectColor{ true, { .835886f, .537747f, 0.f } }, EffectColor{ true, { 1.f, .72f, .24f } }, EffectColor{ true, { 1.f, .84f, .45f } }
	};
	struct EnchantmentAppearance
	{
		bool enabled = false;
		EffectColor color{ false, { .0384615f, .123077f, 1.f } };
		bool swordReleasedTrail = true;
		float swordRibbonLifetime = .2f, swordParticleLifetime = .3f, swordRibbonWidth = .5f, swordTrailMomentum = .1f;
		float swordRibbonFadeOut = .25f, swordParticleFadeOut = .4f, swordParticleGravity = .3f;
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
		bool swordReleasedTrail = true;
		float swordRibbonLifetime = .2f, swordParticleLifetime = .3f, swordRibbonWidth = .5f, swordTrailMomentum = .1f;
		float swordRibbonFadeOut = .25f, swordParticleFadeOut = .4f, swordParticleGravity = .3f;
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
		BlockStyle blockStyle = BlockStyle::NormalBlock;
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
		float cutLength = 1.f;
		float slashDistance = defaultSlashDistance;
		float slashSpread = 0.f;
		float slashDensity = 0.f;
		std::array<EffectColor, 7> colors = defaultColors;
		EnchantmentProfiles enchantments;

		bool operator==(const Config&) const = default;
	};
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
			config.swordRibbonLifetime = appearance->swordRibbonLifetime; config.swordParticleLifetime = appearance->swordParticleLifetime;
			config.swordRibbonFadeOut = appearance->swordRibbonFadeOut; config.swordParticleFadeOut = appearance->swordParticleFadeOut;
			config.swordParticleGravity = appearance->swordParticleGravity;
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
