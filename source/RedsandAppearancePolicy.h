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
	inline float GlowStrength(float original, bool originalBaseline, const RedsandSettings::Config& config,
		RedsandSettings::ColorTarget target = RedsandSettings::ColorTarget::Block)
	{
		using enum RedsandSettings::ColorTarget;
		float brightness = config.impactBrightness;
		switch (target) {
		case Hit: brightness = config.hitBrightness; break;
		case Cut: brightness = config.cutBrightness; break;
		default: break;
		}
		return original * (originalBaseline ? 1.f : RedsandSettings::impactBrightnessBaseline) * brightness;
	}
	inline float RateFactor(const RedsandSettings::Config& config, RedsandSettings::ColorTarget target)
	{
		const auto density = target == RedsandSettings::ColorTarget::Hit ? config.hitDensity : config.impactDensity;
		const float styleScale = target == RedsandSettings::ColorTarget::Block ? .60f : 1.f;
		return density * RedsandSettings::impactDensityBaseline * styleScale;
	}
	template <class Sequence, class Activate>
	inline void StartBlockSequenceOnce(float& marker, Sequence* sequence, Activate&& activate)
	{
		if (marker != 0.f || !sequence) { return; }
		if (!sequence->Inactive() || activate(sequence)) { marker = 1.f; }
	}
	inline bool OwnedRateCurve(float t0, float v0, float t1, float v1, float t2, float v2)
	{
		return t0 == 0.f && v0 == 0.f && t1 == rateTagTime1 && v1 >= 0.f && t2 == rateTagTime2 && v2 == v1 * 2.f;
	}
	inline std::optional<RedsandSettings::ColorTarget> Group(std::string_view name)
	{
		constexpr std::string_view prefix = "RSRGBv1_";
		if (name.size() != prefix.size() + 1 || !name.starts_with(prefix)) { return {}; }
		switch (name.back()) {
		case '2': return RedsandSettings::ColorTarget::Hit;
		case '4': return RedsandSettings::ColorTarget::Cut;
		case '5': return RedsandSettings::ColorTarget::Block;
		default: return {};
		}
	}
	inline const std::array<float, 3>& Color(const RedsandSettings::Config& config, RedsandSettings::ColorTarget target)
	{
		const auto i = static_cast<unsigned>(target);
		return config.colors[i].custom ? config.colors[i].rgb : RedsandSettings::defaultColors[i].rgb;
	}
	void Install();
}
