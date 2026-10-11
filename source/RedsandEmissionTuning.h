#pragma once

#include "RedsandSettings.h"

#include <algorithm>

namespace Redsand::Emission
{
	enum class Scalar { BirthRate, Speed, Radius };

	inline float Value(float original, Scalar kind, const RedsandSettings::Config& config)
	{
		switch (kind) {
		case Scalar::BirthRate: return original * config.swordDensity;
		case Scalar::Speed: return original * config.swordSpread;
		case Scalar::Radius: return original * config.swordSize;
		}
		return original;
	}

	template <class Matrix>
	void NarrowSource(Matrix& rotation, float spread)
	{
		const float width = std::max(spread, 0.001f);
		for (auto& row : rotation.entry) {
			row[1] *= width;
			row[2] *= width;
		}
	}
}
