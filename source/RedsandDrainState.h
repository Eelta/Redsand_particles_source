#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
struct RedsandDrainState
{
	float elapsed = 0.f;

	bool BeginDrain()
	{
		if (emissionStopped) { return false; }
		emissionStopped = true;
		return true;
	}

	void Advance(float delta, std::optional<float> simulationTime = {})
	{
		if (!std::isfinite(delta) || delta <= 0.f) { return; }
		elapsed += delta;
		if (!emissionStopped || stalledFadeStart) { return; }

		if (simulationTime && simulationTime == previousSimulationTime) {
			stationaryTime += delta;
			if (stationaryTime >= .75f) { stalledFadeStart = elapsed; }
		} else {
			stationaryTime = 0.f;
		}
		previousSimulationTime = simulationTime;
	}

	bool IsStalled() const { return stalledFadeStart.has_value(); }

	float SafetyAlpha() const
	{
		const float timeoutFade = std::clamp(6.f - elapsed, 0.f, 1.f);
		if (!stalledFadeStart) { return timeoutFade; }
		return std::min(timeoutFade, std::clamp(1.f - (elapsed - *stalledFadeStart) / .25f, 0.f, 1.f));
	}
	bool CanRetire(std::uint32_t liveParticles) const
	{
		return (elapsed >= 0.1f && liveParticles == 0) || SafetyAlpha() == 0.f;
	}

private:
	std::optional<float> previousSimulationTime;
	std::optional<float> stalledFadeStart;
	float stationaryTime = 0.f;
	bool emissionStopped = false;
};
