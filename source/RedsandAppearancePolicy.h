#pragma once
#include "RedsandSettings.h"
#include <optional>
#include <string_view>
#include <algorithm>

namespace RedsandAppearance
{
	inline constexpr std::string_view rootTag = "RSRGBRoot_v1";
	inline constexpr std::string_view lengthTag = "RSRGBCutLength_v1";
	inline constexpr float rateTagTime1 = 1.f / 32768.f;
	inline constexpr float rateTagTime2 = 1.f / 16384.f;
	inline float SlashSpeed(float original, const RedsandSettings::Config& config) { return original * RedsandSettings::slashDistanceBaseline * config.slashDistance; }
	inline float SlashSpread(float original, const RedsandSettings::Config& config) { return original * RedsandSettings::slashSpreadBaseline * config.slashSpread; }
	inline float SlashDensity(const RedsandSettings::Config& config) { return std::min(config.impactDensity * config.slashDensity * RedsandSettings::slashDensityBaseline, RedsandSettings::maxDensity) * RedsandSettings::impactDensityBaseline; }
	inline bool OwnedSlashRateCurve(float t0, float v0, float t1, float v1, float t2, float v2)
	{
		return t0 == 0.f && v0 == 0.f && t1 == rateTagTime1 * .5f && v1 >= 0.f && t2 == rateTagTime2 * .5f && v2 == v1 * 2.f;
	}
	inline bool OwnedRateCurve(float t0, float v0, float t1, float v1, float t2, float v2)
	{
		return t0 == 0.f && v0 == 0.f && t1 == rateTagTime1 && v1 >= 0.f && t2 == rateTagTime2 && v2 == v1 * 2.f;
	}
	inline std::optional<RedsandSettings::ColorTarget> Group(std::string_view name)
	{
		constexpr std::string_view prefix = "RSRGBv1_";
		if (name.size() != prefix.size() + 1 || !name.starts_with(prefix)) { return {}; }
		const auto group = static_cast<unsigned>(name.back() - '0');
		if (group < static_cast<unsigned>(RedsandSettings::ColorTarget::Hit) ||
			group >= static_cast<unsigned>(RedsandSettings::ColorTarget::Count)) { return {}; }
		return static_cast<RedsandSettings::ColorTarget>(group);
	}
	inline const std::array<float, 3>& Color(const RedsandSettings::Config& config, RedsandSettings::ColorTarget target)
	{
		const auto i = static_cast<unsigned>(target);
		return config.colors[i].custom ? config.colors[i].rgb : RedsandSettings::defaultColors[i].rgb;
	}
	void Install();
}
