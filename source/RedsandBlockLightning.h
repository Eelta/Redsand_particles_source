#pragma once
#include <cstdint>
namespace RE { class TESObjectREFR; class NiPoint3; }
namespace RedsandBlockLightning
{
    void Initialize();
    void Update();
    void Reset();
    void Notify(RE::TESObjectREFR* target, RE::TESObjectREFR* cause, const RE::NiPoint3& point);
}
