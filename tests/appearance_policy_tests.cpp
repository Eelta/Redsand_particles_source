#include "../source/RedsandAppearancePolicy.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
struct BlockSequence
{
	bool inactive = true;
	unsigned attempts = 0;
	bool Inactive() const { return inactive; }
};
int main()
{
	using namespace RedsandAppearance;
	using namespace RedsandSettings;
	BlockSequence sequence;
	float started = 0.f;
	bool succeeds = false;
	auto activate = [&](BlockSequence* candidate) {
		++candidate->attempts;
		if (succeeds) { candidate->inactive = false; }
		return succeeds;
	};
	StartBlockSequenceOnce(started, static_cast<BlockSequence*>(nullptr), activate);
	if (started != 0.f || sequence.attempts != 0) { throw std::runtime_error("missing ring sequence changed activation state"); }
	StartBlockSequenceOnce(started, &sequence, activate);
	if (started != 0.f || sequence.attempts != 1) { throw std::runtime_error("failed ring activation could not retry"); }
	succeeds = true;
	StartBlockSequenceOnce(started, &sequence, activate);
	if (started != 1.f || sequence.attempts != 2) { throw std::runtime_error("ring sequence failed to activate"); }
	sequence.inactive = true;
	for (unsigned frame = 0; frame < 1000; ++frame) { StartBlockSequenceOnce(started, &sequence, activate); }
	if (sequence.attempts != 2) { throw std::runtime_error("ring sequence restarted on later updates"); }
	float alreadyActive = 0.f;
	sequence.inactive = false;
	StartBlockSequenceOnce(alreadyActive, &sequence, activate);
	if (alreadyActive != 1.f || sequence.attempts != 2) { throw std::runtime_error("already playing ring sequence was restarted"); }
	BlockSequence reloaded;
	float reloadedState = 0.f;
	StartBlockSequenceOnce(reloadedState, &reloaded, activate);
	if (reloadedState != 1.f || reloaded.attempts != 1 || reloaded.inactive) { throw std::runtime_error("fresh ring instance could not activate independently"); }
	for (auto bad : { "", "RSRGBv1_", "RSRGBv1_0", "RSRGBv1_1", "RSRGBv1_3", "RSRGBv1_6", "RSRGBv1_7", "RSRGBv1_40", "OtherRSRGBv1_4", "RSRGBv1_x" }) {
		if (Group(bad)) { throw std::runtime_error("foreign or malformed shader tag accepted"); }
	}
	if (Group("RSRGBv1_2") != ColorTarget::Hit || Group("RSRGBv1_4") != ColorTarget::Cut ||
		Group("RSRGBv1_5") != ColorTarget::Block) { throw std::runtime_error("owned color group lost"); }
	Config config;
	if (GlowStrength(8.f, true, config) != 8.f || GlowStrength(8.f, false, config) != 80.f) {
		throw std::runtime_error("original ring glow changed the established effect baselines");
	}
	config.impactBrightness = 1.5f;
	for (unsigned frame = 0; frame < 1000; ++frame) {
		if (GlowStrength(8.f, true, config) != 12.f || GlowStrength(8.f, false, config) != 120.f) {
			throw std::runtime_error("live ring glow accumulated its multiplier or changed other styles");
		}
	}
	config.impactBrightness = 1.f;
	config.colors[static_cast<unsigned>(ColorTarget::Cut)].rgb = { .1f, .8f, .3f };
	if (Color(config, ColorTarget::Cut) != config.colors[static_cast<unsigned>(ColorTarget::Cut)].rgb) { throw std::runtime_error("custom cut color lost"); }
	config.colors[static_cast<unsigned>(ColorTarget::Cut)].custom = false;
	if (Color(config, ColorTarget::Cut) != std::array<float, 3>{ .835886f, .537747f, 0.f }) { throw std::runtime_error("captured cut default lost"); }
	config.impactDensity = 2.85f;
	config.hitDensity = 0.f;
	config.hitBrightness = 2.f;
	config.cutBrightness = .25f;
	if (RateFactor(config, ColorTarget::Hit) != 0.f || std::abs(RateFactor(config, ColorTarget::Block) - 28.5f) > .0001f ||
		GlowStrength(8.f, false, config, ColorTarget::Hit) != 160.f ||
		GlowStrength(8.f, false, config, ColorTarget::Cut) != 20.f ||
		GlowStrength(8.f, false, config, ColorTarget::Block) != 80.f) {
		throw std::runtime_error("independent hit, cut and block controls overlapped");
	}
	config.impactBrightness = 1.37f;
	if (!OwnedRateCurve(0.f, 0.f, rateTagTime1, 0.f, rateTagTime2, 0.f) ||
		!OwnedRateCurve(0.f, 0.f, rateTagTime1, 12.f, rateTagTime2, 24.f) ||
		OwnedRateCurve(0.f, 0.f, .01f, 0.f, rateTagTime2, 0.f) ||
		OwnedRateCurve(0.f, 1.f, rateTagTime1, 0.f, rateTagTime2, 0.f)) { throw std::runtime_error("continuous controls leaked into unmarked curves"); }
	for (int i = 0; i < 1000; ++i) {
		const float original = 420.f;
		const float output = original * (OwnedRateCurve(0.f, 0.f, rateTagTime1, 0.f, rateTagTime2, 0.f) ? config.impactDensity : 1.f);
		if (std::abs(output - 1197.f) > .001f) { throw std::runtime_error("cached curve output accumulated density"); }
	}
	Config unit;
	if (unit.impactBrightness != 1.f || unit.impactDensity != .1f || unit.cutLength != 1.f || unit.cutWidth != 1.f) {
		throw std::runtime_error("unit defaults lost");
	}
	std::cout << "Owned shader allowlist, custom RGB and exact default restoration passed\n";
}
