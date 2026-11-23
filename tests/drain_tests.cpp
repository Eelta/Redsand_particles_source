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
	check(first.BeginDrain(), "first trail did not stop emission on entry");
	first.Advance(1.f);
	RedsandDrainState second;
	check(second.BeginDrain(), "new trail inherited an old emission stop");
	second.Advance(0.25f);
	check(!first.BeginDrain() && !second.BeginDrain(), "drain repeated emitter shutdown");
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
	RedsandDrainState frozen;
	frozen.BeginDrain();
	frozen.Advance(.01f, 10.f);
	frozen.Advance(.5f, 10.f);
	check(!frozen.IsStalled(), "briefly stalled trail lost its grace period");
	frozen.Advance(.01f, 10.1f);
	frozen.Advance(.5f, 10.1f);
	check(!frozen.IsStalled(), "a resumed trail carried over its old stall timer");
	frozen.Advance(.25f, 10.1f);
	check(frozen.IsStalled() && frozen.SafetyAlpha() == 1.f, "frozen simulation did not start a graceful fade");
	frozen.Advance(.125f, 10.1f);
	check(frozen.SafetyAlpha() > 0.f && frozen.SafetyAlpha() < 1.f && !frozen.CanRetire(200),
		"frozen particles were cut before fading");
	frozen.Advance(.126f, 10.2f);
	check(frozen.CanRetire(200), "frozen particles retained their old slots after fading");

	RedsandDrainState advancing;
	advancing.BeginDrain();
	for (int step = 0; step < 40; ++step) { advancing.Advance(.1f, static_cast<float>(step)); }
	check(!advancing.IsStalled() && !advancing.CanRetire(200), "an unchanged live count was mistaken for a stall");

	std::vector<RedsandDrainState> trails;
	std::size_t peak = 0;
	std::size_t emissionStops = 0;
	std::size_t drainUpdates = 0;
	for (int frame = 0; frame < 10500; ++frame) {
		if (frame < 10000 && frame % 10 == 0) trails.emplace_back();
		for (auto& trail : trails) {
			if (trail.BeginDrain()) { ++emissionStops; }
			++drainUpdates;
			trail.Advance(0.01f);
		}
		std::erase_if(trails, [](const auto& trail) {
			const auto live = trail.elapsed < 3.3f ? 100u : 0u;
			check(live == 0 || !trail.CanRetire(live), "combo retired surviving particles");
			return trail.CanRetire(live);
		});
		peak = std::max(peak, trails.size());
	}
	check(peak <= 34, "combo retained unbounded instances");
	check(trails.empty(), "trails remained after attacks stopped");
	check(emissionStops == 1000, "emission shutdown was skipped or repeated during rapid attacks");
	check(drainUpdates > 300000, "drain updates stopped after emitter shutdown");
	std::cout << "PASS: one emission shutdown per trail, independent clocks, graceful retirement, bounded fallback, 1000-attack lifecycle simulation\n";
}
