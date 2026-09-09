#include "../source/RedsandDrainState.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

void check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	RedsandDrainState first;
	first.Advance(1.f);
	RedsandDrainState second;
	second.Advance(0.25f);
	check(first.elapsed == 1.f, "new attack reset old attack clock");
	check(!first.CanRetire(1), "retired visible live particle");
	check(first.CanRetire(0), "empty trail retained");
	RedsandDrainState pending;
	check(!pending.CanRetire(0), "no grace for first particle update");
	pending.Advance(-1.f);
	pending.Advance(std::numeric_limits<float>::quiet_NaN());
	check(pending.elapsed == 0.f, "invalid time corrupted drain");
	RedsandDrainState stalled;
	stalled.Advance(5.f);
	check(stalled.SafetyAlpha() == 1.f && !stalled.CanRetire(100), "fallback started early");
	stalled.Advance(0.5f);
	check(stalled.SafetyAlpha() == 0.5f && !stalled.CanRetire(100), "fallback did not fade before removal");
	stalled.Advance(0.5f);
	check(stalled.SafetyAlpha() == 0.f && stalled.CanRetire(100), "stalled trail leaked");
	std::vector<RedsandDrainState> trails;
	std::size_t peak = 0;
	for (int frame = 0; frame < 10500; ++frame) {
		if (frame < 10000 && frame % 10 == 0) trails.emplace_back();
		for (auto& trail : trails) trail.Advance(0.01f);
		std::erase_if(trails, [](const auto& trail) {
			const auto live = trail.elapsed < 3.3f ? 100u : 0u;
			check(live == 0 || !trail.CanRetire(live), "combo retired surviving particles");
			return trail.CanRetire(live);
		});
		peak = std::max(peak, trails.size());
	}
	check(peak <= 34, "combo retained unbounded instances");
	check(trails.empty(), "trails remained after attacks stopped");
	std::cout << "PASS: independent clocks, graceful retirement, bounded fallback, 1000-attack lifecycle simulation\n";
}
