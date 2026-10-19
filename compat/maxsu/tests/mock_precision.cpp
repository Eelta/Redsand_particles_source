#include <atomic>
namespace {
    std::atomic_int state{ 0 };
    constexpr char first[] = "StellarImpact\\Runtime\\B2D2S2\\Block\\NormalBlock.nif";
    constexpr char second[] = "StellarImpact\\Runtime\\B0D1S3\\Block\\NormalBlock.nif";
}
extern "C" __declspec(dllexport) const char* __cdecl Redsand_GetBlockImpactModel_v1() noexcept
{
    const auto value = state.load();
    return value == 1 ? first : value == 2 ? second : nullptr;
}
extern "C" __declspec(dllexport) void __cdecl TestSetState(int value) noexcept { state.store(value); }
