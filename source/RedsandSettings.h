#pragma once

#include <filesystem>

namespace RedsandSettings
{
	enum class Language { English = 0, Chinese = 1 };

	inline constexpr float maxBrightness = 5.0f;
	inline constexpr float maxDensity = 2.0f;
	inline constexpr float defaultSpread = 1.0f;
	inline constexpr float maxSpread = 2.0f;
	inline constexpr float minSize = 0.25f;
	inline constexpr float maxSize = 2.0f;

	struct Config
	{
		Language language = Language::English;
		bool diagnostics = false;
		bool swordEnabled = true;
		bool swordPlayer = true;
		bool swordNPC = true;
		bool swordTip = true;
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
