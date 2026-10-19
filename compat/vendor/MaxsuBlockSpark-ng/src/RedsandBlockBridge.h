#pragma once




#include <Windows.h>
#include <atomic>

namespace RedsandBlockBridge
{
    using ModelGetter = const char* (__cdecl*)() noexcept;
    struct Selection
    {
        const char* model;
        float lifetime;
        bool custom;
    };

    constexpr unsigned SpawnFlags(const Selection& selected) noexcept { return selected.custom ? 7U : 4U; }

    inline ModelGetter Resolve() noexcept
    {
        static std::atomic<ModelGetter> cached{ nullptr };
        auto getter = cached.load(std::memory_order_acquire);
        if (!getter) {
            if (const auto module = GetModuleHandleW(L"Precision.dll")) {
                getter = reinterpret_cast<ModelGetter>(GetProcAddress(module, "Redsand_GetBlockImpactModel_v1"));
                if (getter) { cached.store(getter, std::memory_order_release); }
            }
        }
        return getter;
    }

    inline Selection Select(const char* originalModel) noexcept
    {
        if (const auto getter = Resolve()) {
            if (const auto model = getter(); model && model[0]) {


                return { model, 0.85f, true };
            }
        }
        return { originalModel, 0.0f, false };
    }
}
