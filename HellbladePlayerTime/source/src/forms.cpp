#include "forms.hpp"
#include "settings.hpp"

void Forms::Load() {
    const auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler == nullptr)
        return;

    constexpr const std::string_view SKYRIM_PLUGIN_NAME = "Skyrim.esm"sv;
    ActionBlockHit = dataHandler->LookupForm<RE::BGSAction>(0x13AF4, SKYRIM_PLUGIN_NAME);
    ActionLargeRecoil = dataHandler->LookupForm<RE::BGSAction>(0x13EC8, SKYRIM_PLUGIN_NAME);







    constexpr const std::string_view PLUGIN_NAME = "HellbladeTimedBlock.esp"sv;
    HB_DamageBoostEnabled = dataHandler->LookupForm<RE::TESGlobal>(0x808, PLUGIN_NAME);
    HB_TimedBlock01SD = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x80C, PLUGIN_NAME);
    HB_TimedBlock02SD = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x80D, PLUGIN_NAME);
    HB_PerfectBlockSD = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x804, PLUGIN_NAME);
    HB_DamageBoostEffect = dataHandler->LookupForm<RE::EffectSetting>(0x80B, PLUGIN_NAME);
    HB_PerfectBlockEffect = dataHandler->LookupForm<RE::EffectSetting>(0x802, PLUGIN_NAME);
    HB_PerfectBlockSpell = dataHandler->LookupForm<RE::SpellItem>(0x803, PLUGIN_NAME);


    std::vector<RE::TESForm*> forms {
        HB_DamageBoostEnabled, HB_TimedBlock01SD, HB_TimedBlock02SD, HB_PerfectBlockSD, HB_DamageBoostEffect, HB_PerfectBlockEffect, HB_PerfectBlockSpell
    };

    if (const auto missing = std::ranges::count_if(forms, [](auto form) { return form == nullptr; }); missing > 0) {
        const auto err = fmt::format("Could not find {} forms in {}"sv, missing, PLUGIN_NAME);
        stl::report_and_error(err);
    }
}

void Forms::Update() {
    HB_DamageBoostEnabled->value = Settings::bPerfectBlockDamageMultEnabled ? 1.f : 0.f;
    for (auto& eff : HB_PerfectBlockSpell->effects) {
        if (eff->baseEffect != Forms::HB_DamageBoostEffect)
            continue;

        eff->effectItem.duration = Settings::iPerfectBlockDamageMultDuration;
        logger::info("Set duration of perfect block effect to {} seconds"sv, Settings::iPerfectBlockDamageMultDuration);
        break;
    }
}
