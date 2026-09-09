#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
struct RedsandDrainState
{
	float elapsed = 0.f;

	void Advance(float delta)
	{
		if (std::isfinite(delta) && delta > 0.f) {
			elapsed += delta;
		}
	}

	float SafetyAlpha() const { return std::clamp(6.f - elapsed, 0.f, 1.f); }
	bool CanRetire(std::uint32_t liveParticles) const
	{
		return (elapsed >= 0.1f && liveParticles == 0) || SafetyAlpha() == 0.f;
	}
};
