#include "RedsandBlockBridge.h"
#include <cstring>
#include <iostream>
int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    constexpr auto weapon = "Maxsu\\SimpleBlockSpark\\fxmetalsparkimpactweap.nif";
    constexpr auto shield = "Maxsu\\SimpleBlockSpark\\fxmetalsparkimpactshield.nif";
    for (const auto original : { weapon, shield }) {
        const auto result = RedsandBlockBridge::Select(original);
        if (result.model != original || result.lifetime != 0 || result.custom) return 2;
    }
    const auto module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    const auto set = reinterpret_cast<void(__cdecl*)(int) noexcept>(GetProcAddress(module,"TestSetState"));
    if (!set || !RedsandBlockBridge::Resolve()) return 4;
    set(1);
    const auto first = RedsandBlockBridge::Select(weapon);
    if (!first.custom || first.lifetime != .85f || !std::strstr(first.model,"B2D2S2")) return 5;
    set(2);
    const auto second = RedsandBlockBridge::Select(shield);
    if (!second.custom || !std::strstr(second.model,"B0D1S3") || !std::strstr(first.model,"B2D2S2")) return 6;
    set(0);
    for (const auto original : { weapon, shield }) {
        const auto result = RedsandBlockBridge::Select(original);
        if (result.model != original || result.lifetime != 0 || result.custom) return 7;
    }
    if (RedsandBlockBridge::SpawnFlags(first) != 7U || RedsandBlockBridge::SpawnFlags({weapon,0.f,false}) != 4U) return 8;

    std::cout << "Absent DLL, late resolution, live preset changes, stable returned model, and both fallback paths passed.\n";
    return 0;
}
