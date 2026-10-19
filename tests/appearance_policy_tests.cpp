#include "../source/RedsandAppearancePolicy.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
int main()
{
	using namespace RedsandAppearance;
	using namespace RedsandSettings;
	for (auto bad : { "", "RSRGBv1_", "RSRGBv1_0", "RSRGBv1_1", "RSRGBv1_7", "RSRGBv1_40", "OtherRSRGBv1_4", "RSRGBv1_x" }) {
		if (Group(bad)) { throw std::runtime_error("foreign or malformed shader tag accepted"); }
	}
	for (unsigned i = 2; i < 7; ++i) {
		const auto group = Group(std::string("RSRGBv1_") + char('0' + i));
		if (!group || static_cast<unsigned>(*group) != i) { throw std::runtime_error("owned color group lost"); }
	}
	Config config;
	config.colors[4].rgb = { .1f, .8f, .3f };
	if (Color(config, ColorTarget::Cut) != config.colors[4].rgb) { throw std::runtime_error("custom cut color lost"); }
	config.colors[4].custom = false;
	if (Color(config, ColorTarget::Cut) != std::array<float, 3>{ .835886f, .537747f, 0.f }) { throw std::runtime_error("captured cut default lost"); }
	config.impactDensity = 2.85f;
	config.impactBrightness = 1.37f;
	config.slashDistance = 3.25f;
	for (int i = 0; i < 1000; ++i) {
		if (SlashSpeed(430.f, config) != 27950.f) { throw std::runtime_error("slash speed accumulated or ignored fractional distance"); }
	}
	config.slashDistance = .1f;
	if (SlashSpeed(430.f, config) != 860.f || config.impactDensity != 2.85f || config.impactBrightness != 1.37f) {
		throw std::runtime_error("slash distance changed density/brightness or retained an old speed");
	}
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
    if (unit.impactBrightness != 1.f || unit.impactDensity != 1.f || unit.cutLength != 1.f || unit.slashDistance != 1.f || unit.slashSpread != 1.f || unit.slashDensity != 1.f) { throw std::runtime_error("unit defaults lost"); }
    if (SlashSpeed(430.f, unit) != 8600.f || SlashDensity(unit) != 100.f || std::abs(SlashSpread(.075f, unit) - .075f * .65f * .2f) > 1e-6f) { throw std::runtime_error("captured travel/count/spread not preserved at 1x"); }
    for (float global : { 0.f, .1f, .5f, 1.f, 2.f, 10.f }) {
        unit.impactDensity = global;
        const auto oldCapturedCount = std::min(global * 10.f, 10.f) * 10.f;
        if (SlashDensity(unit) != oldCapturedCount) { throw std::runtime_error("rebased count differs from captured old slash10 configuration"); }
    }
    unit.impactDensity = 1.f;
    unit.slashSpread=0.f;
    if (SlashSpread(.18f, unit) != 0.f) { throw std::runtime_error("straight-line spray retained angular spread"); }
    unit.slashDensity=.18f; unit.impactDensity=2.85f;
    if (std::abs(SlashDensity(unit)-51.3f)>1e-4f || unit.slashDistance!=1.f) { throw std::runtime_error("independent slash count ignored fractional values or changed travel"); }
    unit.slashDensity=maxSlashDensity; unit.impactDensity=maxDensity;
    if (SlashDensity(unit)!=100.f) { throw std::runtime_error("documented combined count cap failed"); }
    if (maxSlashDensity != 1.f) { throw std::runtime_error("slash quantity range increased the established absolute limit"); }
    for (float global : { 0.f, .25f, 1.f, 5.f, maxDensity }) {
        for (float slash : { 0.f, .1f, .3f, maxSlashDensity }) {
            unit.impactDensity=global; unit.slashDensity=slash;
            if (SlashDensity(unit) < 0.f || SlashDensity(unit) > 100.f) { throw std::runtime_error("rebased independent/global count exceeded original capacity budget"); }
        }
    }
    if (!OwnedSlashRateCurve(0.f,0.f,rateTagTime1*.5f,12.f,rateTagTime2*.5f,24.f) || OwnedRateCurve(0.f,0.f,rateTagTime1*.5f,12.f,rateTagTime2*.5f,24.f)) { throw std::runtime_error("slash count leaked into other effects"); }
	std::cout << "Owned shader allowlist, custom RGB and exact default restoration passed\n";
}
