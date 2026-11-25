#define REDSAND_LOG_STANDALONE
#include "../source/RedsandLog.h"

namespace RedsandLog
{
    inline void Sync() noexcept {}
    inline void info(std::string_view) noexcept {}
    inline void warn(std::string_view) noexcept {}
}

#include "../source/RedsandMenu.cpp"
#include <cstdlib>
#include <iostream>
#include <vector>

RedsandImpactVFX::Status RedsandImpactVFX::GetStatus()
{
    Status status;
    status.availability = Availability::Ready;
    status.blockAvailable = true;
    return status;
}

namespace
{
    std::vector<std::string> controls;
    std::string_view tab, edited;
    int choice = 0;
    int tabDepth = 0;
    float shownArcWidth = -1.f;

    void Check(bool condition, const char* reason)
    {
        if (!condition) { std::cerr << reason << '\n'; std::exit(1); }
    }

    bool Record(const char* label)
    {
        const std::string_view full(label);
        const auto separator = full.find("###");
        const auto id = separator == full.npos ? full : full.substr(separator + 3);
        controls.emplace_back(id);
        return id == edited;
    }

    bool Has(std::string_view id) { return std::ranges::find(controls, id) != controls.end(); }
    void Frame(std::string_view active, std::string_view edit = {}, int next = 0)
    {
        tab = active; edited = edit; choice = next; controls.clear();
        RedsandMenu::Render();
        Check(tabDepth == 0, "the menu left an unclosed tab item");
    }
}

int main()
{
    const auto ini = std::filesystem::temp_directory_path() /
        ("redsand-menu-test-" + std::to_string(GetCurrentProcessId()) + ".ini");
    RedsandSettings::Load(ini);
    auto& ui = RedsandMenu::menu;
    ui.SetNextItemWidth = +[](float) {};
    ui.SameLine = +[](float, float) {};
    ui.IsItemHovered = +[](int) { return false; };
    ui.BeginTabBar = +[](const char*, int) { return true; };
    ui.EndTabBar = +[]() { Check(tabDepth == 0, "the tab bar closed before its item"); };
    ui.BeginTabItem = +[](const char* label, bool*, int) {
        if (!std::string_view(label).ends_with(tab)) return false;
        ++tabDepth;
        return true;
    };
    ui.EndTabItem = +[]() { --tabDepth; };
    ui.Checkbox = +[](const char* label, bool* value) {
        if (!Record(label)) return false;
        *value = !*value;
        return true;
    };
    ui.SliderFloat = +[](const char* label, float* value, float minimum, float maximum, const char*, int) {
        const std::string_view caption(label);
        if (caption.ends_with("###swordRibbonWidth") || caption.ends_with("###categoryArcWidth")) {
            Check(minimum == 0.f && maximum == 2.f, "the arc-width slider has the wrong range");
            shownArcWidth = *value;
        }
        if (!Record(label)) return false;
        *value = static_cast<float>(choice);
        return true;
    };
    ui.Combo = +[](const char* label, int* value, const char*, int) {
        if (!Record(label)) return false;
        *value = choice;
        return true;
    };
    ui.SmallButton = +[](const char* label) { return Record(label); };
    ui.TextUnformatted = +[](const char*, const char*) {};
    ui.Separator = +[]() {};
    ui.PushTextWrapPos = +[](float) {};
    ui.PopTextWrapPos = +[]() {};
    ui.CollapsingHeader = +[](const char*, int) { return false; };

    Frame("redsandMelee");
    Check(Has("swordSize") && !Has("redsandSize") && !Has("categoryEnabled"), "global particle list is mixed with other settings");
    Frame("redsandMelee", "swordMeleeMode", 1);
    Check(Has("redsandSize") && Has("redsandArcBrightness") && Has("swordSize") && !Has("swordKeepPrecisionTrail") &&
        Has("redsandDensity") && !Has("redsandSpacing"),
        "Redsand should show both independent settings groups when ordinary particles are enabled");
    Check(shownArcWidth == 1.f, "the default physical width does not show up as 1");
    Frame("redsandMelee", "swordRibbonWidth", 2);
    Check(RedsandSettings::Get().swordRibbonWidth == .30f, "2x did not double the default arc width");
    Frame("redsandMelee", "swordRibbonWidth", 0);
    Check(RedsandSettings::Get().swordRibbonWidth == 0.f, "the width cannot be set to zero");
    Frame("redsandMelee", "swordEnabled");
    Check(Has("redsandSize") && !Has("swordSize"), "disabling ordinary particles hid or changed the Redsand settings");
    Frame("redsandMelee", "swordEnabled");
    Frame("redsandMelee", "redsandSize", 3);
    auto config = RedsandSettings::Get();
    Check(config.redsand.size == 3.f && config.swordSize == 1.f, "editing Redsand changed ordinary particle size");
    Frame("redsandMelee", "swordSize", 2);
    Frame("redsandMelee", "meleeRGBEnabled");
    config = RedsandSettings::Get();
    Check(config.redsand.size == 3.f && config.swordSize == 2.f && !config.redsand.color.custom &&
        config.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Melee)].custom,
        "editing ordinary particles changed Redsand size or color");
    Frame("redsandMelee", "redsandArcBrightness", 4);
    Frame("redsandMelee", "swordBrightness", 6);
    Frame("redsandMelee", "redsandParticleBrightness", 2);
    config = RedsandSettings::Get();
    Check(config.redsand.arcBrightness == 4.f && config.redsand.particleBrightness == 2.f && config.swordBrightness == 6.f,
        "the three visible brightness sliders did not edit separate settings");
    Frame("redsandMelee", "swordMeleeMode", 0);
    Check(Has("swordSize") && !Has("redsandSize") && RedsandSettings::Get().swordEnabled,
        "switching sword trails changed the ordinary-particle checkbox");
    Frame("redsandMelee", "meleeEnchantmentScope", 2);
    Check(Has("categoryEnabled") && !Has("categorySwordSize") && !Has("redsandSize"), "editing controls appeared for an inherited category");
    Frame("redsandMelee", "categoryEnabled");
    Check(Has("categoryMeleeMode") && Has("categorySwordSize") && !Has("categoryBowSize"), "melee category shows unrelated controls");
    Frame("redsandMelee", "categoryMeleeMode", 1);
    Check(Has("categoryRedSize") && Has("categorySwordSize") && Has("categoryRedDensity") && !Has("categoryRedSpacing"),
        "the category did not show its independent particle groups and unified density");
    Check(shownArcWidth == 1.f, "a new category did not use the same 1x arc width");
    Frame("redsandBow");
    Check(Has("bowEnabled") && !Has("categoryEnabled"), "bow scope inherited the selected melee category");
    Frame("redsandImpactVFX");
    Check(Has("blockLightningChance") && Has("impactDensity") && !Has("blockStyle") && !Has("categoryEnabled"),
        "the impact page did not show ordinary sparks and lightning directly");

    RedsandSettings::FlushPendingSave();
    std::filesystem::remove(ini);
    return 0;
}
