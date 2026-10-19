#include "../source/RedsandBlockLightningPolicy.h"
#include "../source/RedsandSettings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    using namespace RedsandBlockLightning;
    Check(!Selected(false, false, 1, 0), "disabled flash triggered");
    Check(Selected(true, false, 0, .99f), "every-block mode used probability");
    Check(!Selected(true, true, 0, 0) && Selected(true, true, 1, .999f), "probability endpoints failed");
    Gate gate; Check(gate.Accept(1, 10) && !gate.Accept(1, 10.001) && gate.Accept(2, 10.001), "duplicate hit event rolled twice or different attacker lost");
    Check(gate.Accept(1, 10.12), "next actual blocked swing suppressed");
    for (unsigned i = 0; i < pieceCount; ++i) {
        Check(Animate(i, 0).alpha == 0 && Animate(i, duration).alpha == 0, "birth/death flashes instead of fading");
        float peak = 0;
        const auto piece = Describe(i);
        for (int f = 0; f <= 310; ++f) {
            auto pose = Animate(i, f*.005f); peak = std::max(peak, Opacity(pose, (piece.begin+piece.end)*.5f));
            Check(std::isfinite(pose.x+pose.y+pose.z+MaskScale(pose)+MaskOffset(pose)) && pose.alpha >= 0 && pose.alpha <= 1, "invalid pose");
            Check(pose.x == 0.f && pose.y == 0.f && pose.z == 0.f, "stationary lightning path is oscillating");
        }
        Check(peak > .9f, "section never receives the travelling current");
    }
    const auto first = Schedule(0), fork = Schedule(1), twig = Schedule(2);
    const auto front = Animate(0, first.birth+first.travel*.5f);
    Check(Opacity(front, .2f) > .9f && Opacity(front, .8f) == 0, "whole path scaled/faded instead of revealing its leading section");
    const auto retreat = Animate(0, first.birth+first.tailDelay+first.tailTravel*.5f);
    Check(Opacity(retreat, .1f) == 0 && Opacity(retreat, .8f) > .5f, "tail did not extinguish progressively from root to tip");
    const auto parentAtFork = Animate(0, fork.birth);
    Check(parentAtFork.head > .3958333f && fork.birth < twig.birth, "fork appeared before current reached attachment");
    Check(Schedule(5).birth != first.birth && Schedule(10).birth != Schedule(5).birth, "all primary arcs start together");
    using namespace RedsandSettings;
    const auto file = std::filesystem::temp_directory_path()/"redsand-block-lightning-test.ini";
    { std::ofstream out(file); out << "[Menu]\nLanguage=Chinese\n[ImpactVFX]\nBrightness=2.5\n[BlockLightning]\nEnabled=true\nRandomTrigger=false\nChance=.37\nRandomColor=true\nBrightness=3\nSize=1.7\nR=.25\nG=.6\nB=0\n"; }
    Load(file); auto c = Get();
    Check(c.blockLightning && !c.blockLightningRandomTrigger && c.blockLightningRandomColor && c.blockLightningChance == .37f, "menu settings not parsed");
    Check(c.language == Language::Chinese && c.impactBrightness == 2.5f && c.blockLightningSize == 1.7f, "existing options changed");
    Check(c.blockLightningColor == std::array<float, 3>{.25f, .6f, 0.f}, "default RGB did not load or zero was discarded");
    c.blockLightningChance = 2; c.blockLightningBrightness = std::numeric_limits<float>::quiet_NaN(); Set(c); FlushPendingSave(); Load(file);
    Check(Get().blockLightningChance == 1 && Get().blockLightningBrightness == 1 && Get().blockLightningSize == 1.7f, "normalization/roundtrip failed");
    Check(Get().blockLightningColor == c.blockLightningColor, "default color did not roundtrip with random mode enabled");
    c = Get(); c.blockLightningRandomColor = false; c.blockLightningColor = {2.f, -.2f, std::numeric_limits<float>::quiet_NaN()}; Set(c); FlushPendingSave(); Load(file);
    Check(!Get().blockLightningRandomColor && Get().blockLightningColor == std::array<float, 3>{1.f, 0.f, 1.f}, "RGB clamps/defaults or random-color independence failed");
    std::filesystem::remove(file);
    if (argc == 3 && std::string_view(argv[1]) == "--frames") {
        std::ofstream out(argv[2]); out.precision(8);
        out << "{\"fps\":60,\"duration\":" << duration << ",\"frames\":[";
        for (unsigned f = 0; f <= 93; ++f) {
            if (f) out << ','; out << '[';
            for (unsigned i = 0; i < pieceCount; ++i) {
                if (i) out << ','; const auto p = Animate(i, f/60.f);
                out << '[' << p.head << ',' << p.tail << ',' << p.alpha << ',' << p.x << ',' << p.y << ',' << p.z << ']';
            }
            out << ']';
        }
        out << "]}"; out.close(); Check(bool(out), "production animation frame export failed");
    }
    std::cout << "Independent visual probability, duplicate filtering, smooth fade and persistent settings passed\n";
}
