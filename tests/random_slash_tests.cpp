#include "../source/RedsandSlashRandom.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <limits>

using namespace RedsandSettings;
using namespace RedsandSlashRandom;
void Check(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
int main()
{
    try {
        Config base;
        for (float invalid : {0.f, .5f, -4.5f, 16777216.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            Check(!ReadSceneSeed(invalid), "invalid or foreign scene seed was accepted");
        }
        Check(ReadSceneSeed(-16777215.f)->contact && ReadSceneSeed(-16777215.f)->value == 16777215u &&
            !ReadSceneSeed(1.f)->contact, "paired-contact identity boundary failed");
        Check(!base.slashRandomColor && Sample(base, 44) == base, "disabled mode changed settings");
        base.slashRandomColor = true;
        base.impactSize = 2.f; base.cutLength = 3.f;
        base.impactBrightness = 7.3f;
        base.colors[3] = { false, { .1f, .9f, .2f } };
        base.colors[4] = { true, { .2f, .4f, .8f } };
        const auto untouched = base;
        const auto first = Sample(base, 1);
        bool varied = false;
        std::array<bool, 3> dominant{};
        for (unsigned seed = 1; seed <= 20000; ++seed) {
            auto result = Sample(base, seed);
            Check(result == Sample(base, seed), "same attack changed during playback");
            varied |= result != first;
            const auto& rgb = result.colors[3].rgb;
            for (float channel : rgb) { Check(std::isfinite(channel) && channel >= 0 && channel <= 1, "invalid random RGB"); }
            const auto peak = std::max_element(rgb.begin(), rgb.end());
            Check(*peak == 1.f, "random RGB dimmed its emission peak");
            dominant[peak - rgb.begin()] = true;
            Check(result.colors[3].custom, "local randomized color was not applied");


            result.colors[3] = base.colors[3];
            Check(result == base && base == untouched, "a non-color setting or saved base changed");
            auto contact = Sample(base, seed, true);
            Check(contact.colors[2] == contact.colors[3],
                "paired contact sparks did not share slash RGB");
            contact.colors[2] = base.colors[2]; contact.colors[3] = base.colors[3];
            Check(contact == base, "paired slash contact changed unrelated settings");
        }
        Check(varied && dominant[0] && dominant[1] && dominant[2], "random colors did not span RGB");
        base.impactBrightness = 0;
        base.colors[3] = {true, {0, 0, 0}};
        for (unsigned seed = 1; seed <= 1000; ++seed) {
            auto result = Sample(base, seed);
            Check(result.impactBrightness == 0, "disabled numeric parameter was reenabled");
            auto storedSeed = NextSeed();
            Check(storedSeed > 0 && storedSeed <= 0xffffffu && static_cast<unsigned>(static_cast<float>(storedSeed)) == storedSeed, "seed cannot round trip in scene metadata");
        }
        base.slashRandomColor = false;
        Check(Sample(base, 1) == base && Sample(base, 274) == base, "turning off did not restore manual colors");
        Check(Sample(base, 1, true) == base, "paired contact did not restore manual colors when disabled");
        base.slashRandomValues = true;
        for (float range : {0.f, .25f, .5f, 1.f}) {
            base.slashRandomRange = range;
            base.impactSize = 2.f; base.cutLength = 3.f;
            base.hitDensity = 2.f; base.hitSize = 3.f;
            base.impactBrightness = 7.3f;
            bool numbersVaried = false;
            for (unsigned seed=1; seed<=10000; ++seed) {
                auto sampled = Sample(base, seed);
                const auto contact = Sample(base, seed, true);
                Check(sampled == Sample(base, seed), "numeric sample changed within the same attack");
                auto bounded = [&](float actual, float setting) {
                    Check(actual >= setting*(1.f-range)-.00001f && actual <= setting*(1.f+range)+.00001f, "numeric sample exceeded selected percent range");
                };
                bounded(sampled.impactSize, base.impactSize); bounded(sampled.cutLength, base.cutLength);
                bounded(contact.hitDensity, base.hitDensity); bounded(contact.hitSize, base.hitSize);
                Check(contact.impactDensity == base.impactDensity && contact.impactSize == base.impactSize &&
                    contact.cutLength == base.cutLength, "contact randomization changed unrelated settings");
                Check(sampled.colors == base.colors && contact.colors == base.colors, "numeric-only mode changed colors");
                Check(sampled.impactBrightness == base.impactBrightness && contact.impactBrightness == base.impactBrightness, "numeric mode randomized brightness");
                numbersVaried |= sampled != base;
                sampled.impactSize=base.impactSize; sampled.cutLength=base.cutLength;
                Check(sampled == base, "numeric mode changed unrelated parameters");
                if (range==0.f) { Check(contact == base, "zero range did not disable numeric variation"); }
            }
            Check(numbersVaried == (range>0.f), "numeric range did not control variation");
        }
        base.slashRandomColor = true;
        const auto both = Sample(base, 5);
        base.slashRandomValues = false;
        Check(both.colors == Sample(base, 5).colors, "numeric toggle changed the attack RGB");
        base.slashRandomValues = true; base.hitDensity=base.hitBrightness=base.cutBrightness=0;
        for (unsigned seed=1; seed<=1000; ++seed) {
            const auto sampled = Sample(base, seed, true);
            Check(sampled.hitDensity==0 && sampled.hitBrightness==0 && sampled.cutBrightness==0, "numeric mode reenabled a zero parameter");
        }
        {
            AttackScope firstAttack(17); AppearanceScope firstAppearance(base);
            { AttackScope secondAttack(21); AppearanceScope secondAppearance(untouched); Check(attackSeed == 21 && appearance==&untouched, "nested attack failed"); }
            Check(attackSeed == 17 && appearance==&base, "nested attack did not restore");
            bool isolated = false;
            std::thread other([&] { isolated = attackSeed == 0 && appearance==nullptr; }); other.join();
            Check(isolated, "thread-local attack leaked");
        }
        Check(attackSeed == 0 && appearance==nullptr, "seed escaped its attack");
        std::cout << "20,000 deterministic attack colors: shared cut/sparks RGB; unchanged numerical settings, other colors and saved configuration; isolated scopes passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
