#pragma once

struct Forms
{






    static inline RE::BGSAction* ActionBlockHit;
    static inline RE::BGSAction* ActionLargeRecoil;

    static inline RE::TESGlobal*              HB_DamageBoostEnabled;

    static inline RE::BGSSoundDescriptorForm* HB_TimedBlock01SD;
    static inline RE::BGSSoundDescriptorForm* HB_TimedBlock02SD;
    static inline RE::BGSSoundDescriptorForm* HB_PerfectBlockSD;
    static inline RE::EffectSetting*          HB_DamageBoostEffect;
    static inline RE::EffectSetting*          HB_PerfectBlockEffect;
    static inline RE::SpellItem*              HB_PerfectBlockSpell;


    static void Load();
    static void Update();
};
