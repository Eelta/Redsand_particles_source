#pragma once
#include "RedsandSettings.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace RedsandSlashRandom
{
	inline thread_local std::uint32_t attackSeed = 0;
	inline thread_local const RedsandSettings::Config* appearance = nullptr;
	inline bool Enabled(const RedsandSettings::Config& config) { return config.slashRandomColor || config.slashRandomValues; }
	struct SceneSeed { std::uint32_t value; bool contact; };
	inline std::optional<SceneSeed> ReadSceneSeed(float stored)
	{
		const float value = std::abs(stored);
		if (!std::isfinite(value) || value < 1.f || value > 16777215.f || std::trunc(value) != value) { return std::nullopt; }
		return SceneSeed{ static_cast<std::uint32_t>(value), stored < 0.f };
	}
	inline std::uint32_t Mix(std::uint32_t value)
	{
		value ^= value >> 16; value *= 0x7feb352du;
		value ^= value >> 15; value *= 0x846ca68bu;
		return value ^ (value >> 16);
	}
	inline std::uint32_t NextSeed()
	{
		static const auto nonce = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		static std::atomic<std::uint32_t> sequence{ 1 };
		const auto value = Mix(sequence.fetch_add(1, std::memory_order_relaxed) ^ nonce) & 0xffffffu;
		return value ? value : 1u;
	}
	struct AttackScope
	{
		std::uint32_t previous = attackSeed;
		explicit AttackScope(std::uint32_t seed) { attackSeed = seed; }
		~AttackScope() { attackSeed = previous; }
	};
	struct Generator
	{
		std::uint32_t state;
		float Unit()
		{
			state += 0x9e3779b9u;
			return float(Mix(state) >> 8) / 16777215.f;
		}
	};
	struct AppearanceScope
	{
		const RedsandSettings::Config* previous = appearance;
		explicit AppearanceScope(const RedsandSettings::Config& config) { appearance = &config; }
		~AppearanceScope() { appearance = previous; }
	};
	inline std::array<float, 3> AttackColor(std::uint32_t seed)
	{
		Generator random{ seed };
		std::array<float, 3> rgb{ random.Unit(), random.Unit(), random.Unit() };
		const float peak = std::max({ rgb[0], rgb[1], rgb[2] });
		if (peak <= 0.f) { return { 1.f, 1.f, 1.f }; }


		for (auto& channel : rgb) { channel /= peak; }
		return rgb;
	}
	inline RedsandSettings::Config Sample(const RedsandSettings::Config& base, std::uint32_t seed, bool slashContact = false)
	{
		if (!Enabled(base)) { return base; }
		auto result = base;
		if (base.slashRandomColor) {
			const auto color = AttackColor(seed);
			if (slashContact) {
				const auto hit = static_cast<unsigned>(RedsandSettings::ColorTarget::Hit);
				result.colors[hit] = { true, color };
			}
			for (const auto target : { RedsandSettings::ColorTarget::SlashSparks, RedsandSettings::ColorTarget::Cut }) {
				const auto index = static_cast<unsigned>(target);
				result.colors[index].rgb = color;
				result.colors[index].custom = true;
			}
		}
		if (base.slashRandomValues) {
			Generator random{ seed ^ 0x5a17c9e3u };
			const float range = std::isfinite(base.slashRandomRange) ? std::clamp(base.slashRandomRange, 0.f, 1.f) : .5f;
			auto factor = [&] { return 1.f + (random.Unit() * 2.f - 1.f) * range; };
			auto vary = [&](float value, float low, float high) { return std::clamp(value * factor(), low, high); };
			result.impactSize = vary(base.impactSize, RedsandSettings::minSize, RedsandSettings::maxSize);
			result.cutLength = vary(base.cutLength, RedsandSettings::minCutLength, RedsandSettings::maxCutLength);
			result.slashDistance = vary(base.slashDistance, RedsandSettings::minSlashDistance, RedsandSettings::maxSlashDistance);
			result.slashSpread = vary(base.slashSpread, 0.f, RedsandSettings::maxSlashSpread);
			const float count = factor();
			result.slashDensity = std::clamp(base.slashDensity * count, 0.f, RedsandSettings::maxSlashDensity);


			if (slashContact) { result.impactDensity = std::clamp(base.impactDensity * count, 0.f, RedsandSettings::maxDensity); }
		}
		return result;
	}
}
