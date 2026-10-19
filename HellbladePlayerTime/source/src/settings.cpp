#include "forms.hpp"
#include "settings.hpp"
#include "util.hpp"

void Settings::ReadSettings() {
    constexpr auto settingsPathDefault = L"Data/MCM/Config/HellbladeTimedBlock/settings.ini";
    constexpr auto settingsPath = L"Data/MCM/Settings/HellbladeTimedBlock.ini";

    auto ReadMCMSettings = [&](std::filesystem::path path) {
        const auto pathStr = path.string();
        if (!std::filesystem::exists(path)) {
            logger::warn("Did not find config at: {}"sv, pathStr);
            return;
        }

        CSimpleIniA ini;
        ini.SetUnicode();

        if (ini.LoadFile(path.c_str()) < SI_OK) {
            logger::warn("Failed to load settings from: {}"sv, pathStr);
            return;
        }

        ReadSettingBool(ini, "Settings", "bTimedBlockEnabled", bTimedBlockEnabled);
        ReadSettingFloat(ini, "Settings", "fTimedBlockSlowdownMult", fTimedBlockSlowdownMult);
        ReadSettingFloat(ini, "Settings", "fPerfectBlockSlowdownMult", fPerfectBlockSlowdownMult);
        fTimedBlockSlowdownMult=std::clamp(fTimedBlockSlowdownMult,.05f,1.f);
        fPerfectBlockSlowdownMult=std::clamp(fPerfectBlockSlowdownMult,.05f,1.f);
        ReadSettingFloat(ini, "Settings", "fTimedBlockSlowdownDuration", fTimedBlockSlowdownDuration);
        ReadSettingFloat(ini, "Settings", "fTimedBlockWindow", fTimedBlockWindow);

        Util::LimitLower(fTimedBlockSlowdownDuration, 0.f);
        Util::LimitLower(fTimedBlockWindow, 0.f);

        ReadSettingBool(ini, "Settings", "bPerfectBlockEnabled", bPerfectBlockEnabled);
        ReadSettingFloat(ini, "Settings", "fPerfectBlockSlowdownDuration", fPerfectBlockSlowdownDuration);
        ReadSettingFloat(ini, "Settings", "fPerfectBlockWindow", fPerfectBlockWindow);

        Util::LimitLower(fPerfectBlockSlowdownDuration, 0.f);
        Util::LimitLower(fPerfectBlockWindow, 0.f);

        ReadSettingBool(ini, "Settings", "bPerfectBlockDamageMultEnabled", bPerfectBlockDamageMultEnabled);
        ReadSettingUInt32(ini, "Settings", "iPerfectBlockDamageMultDuration", iPerfectBlockDamageMultDuration);

        ReadSettingFloat(ini, "Settings", "fCooldownTime", fCooldownTime);
        Util::LimitLower(fCooldownTime, 0.f);

        logger::info("Read settings from: {}"sv, pathStr);
    };

    ReadMCMSettings(settingsPathDefault);
    ReadMCMSettings(settingsPath);



    CSimpleIniA patch;
    patch.SetUnicode();
    constexpr auto patchPath = L"Data/SKSE/Plugins/HellbladePlayerTime.ini";
    if (patch.LoadFile(patchPath) >= SI_OK) {
        ReadSettingFloat(patch, "Time", "BlockDurationMultiplier", blockDurationMultiplier);
        ReadSettingBool(patch, "Attack", "Enabled", attack.enabled);
        ReadSettingBool(patch, "Attack", "CombatOnly", attack.combatOnly);
        ReadSettingFloat(patch, "Attack", "Chance", attack.chance);
        ReadSettingFloat(patch, "Attack", "Duration", attack.duration);
        ReadSettingFloat(patch, "Attack", "WorldRate", attack.worldRate);
        ReadSettingFloat(patch, "Attack", "Cooldown", attack.cooldown);
        ReadSettingBool(patch, "Lightning", "Enabled", visual.enabled);
        ReadSettingBool(patch, "Lightning", "OnBlock", visual.onBlock);
        ReadSettingBool(patch, "Lightning", "OnAttack", visual.onAttack);
        ReadSettingBool(patch, "Lightning", "RandomColor", visual.randomColor);
        ReadSettingFloat(patch, "Lightning", "Brightness", visual.brightness);
        ReadSettingFloat(patch, "Lightning", "Density", visual.density);
        ReadSettingFloat(patch, "Lightning", "Red", visual.red);
        ReadSettingFloat(patch, "Lightning", "Green", visual.green);
        ReadSettingFloat(patch, "Lightning", "Blue", visual.blue);
    }
    blockDurationMultiplier = HellbladeTime::FiniteClamp(blockDurationMultiplier, 1.3f, 1.f, 5.f);
    attack.Validate(); visual.Validate();
    logger::info("Player-time patch: block duration x{}, attack enabled={}, chance {}%, duration {}s, world rate {}, legacy lightning={}",
        blockDurationMultiplier, attack.enabled, attack.chance * 100.f, attack.duration, attack.worldRate, visual.enabled);
}

void Settings::ReadSettingBool(CSimpleIniA& ini, const char* sectionName, const char* settingName, bool& setting) {
    if (const char* found = ini.GetValue(sectionName, settingName))
        setting = ini.GetBoolValue(sectionName, settingName);
}

void Settings::ReadSettingFloat(CSimpleIniA& ini, const char* sectionName, const char* settingName, float& setting) {
    if (const char* found = ini.GetValue(sectionName, settingName))
        setting = static_cast<float>(ini.GetDoubleValue(sectionName, settingName));
}

void Settings::ReadSettingUInt32(CSimpleIniA& ini, const char* sectionName, const char* settingName, uint32_t& setting) {
    if (const char* found = ini.GetValue(sectionName, settingName))
        setting = static_cast<uint32_t>(ini.GetLongValue(sectionName, settingName));
}
