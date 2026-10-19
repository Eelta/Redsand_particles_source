#pragma once

#include <filesystem>
#include <array>

namespace RedsandSettings
{
	enum class Language { English = 0, Chinese = 1 };

	inline constexpr float maxBrightness = 20.0f;
	inline constexpr float maxDensity = 10.0f;
	inline constexpr float defaultSpread = 1.0f;
	inline constexpr float maxSpread = 10.0f;
	inline constexpr float minSize = 0.05f;
	inline constexpr float maxSize = 10.0f;
	inline constexpr float minCutLength = 0.05f;
	inline constexpr float maxCutLength = 20.f;
	inline constexpr float defaultSlashDistance = 1.f;
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

	struct Config
	{
		Language language = Language::English;
		bool diagnostics = false;
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
		bool preserveValhallaRing = true;
		float impactBrightness = 1.f;
		float impactDensity = 1.f;
		float impactSize = 1.f;
		float cutLength = 1.f;
		float slashDistance = defaultSlashDistance;
		float slashSpread = 1.f;
		float slashDensity = 1.f;
		std::array<EffectColor, 7> colors = defaultColors;

		bool operator==(const Config&) const = default;
	};

	[[nodiscard]] Config Get();
	void Set(Config config);
	void ResetDefaults();
	void Load(const std::filesystem::path& path = "Data/SKSE/Plugins/RedsandParticles.ini");

	void TickPendingSave();
	void FlushPendingSave();
	[[nodiscard]] bool SaveFailed();
}
