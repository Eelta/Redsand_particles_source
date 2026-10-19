#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#ifdef NDEBUG
    #include <spdlog/sinks/basic_file_sink.h>
#else
    #include <spdlog/sinks/msvc_sink.h>
#endif

using namespace std::literals;

namespace logger = SKSE::log;

namespace stl
{
    using namespace SKSE::stl;

    template <class T>
    void write_thunk_call(uintptr_t src)
    {
        SKSE::AllocTrampoline(14);

        auto& trampoline = SKSE::GetTrampoline();
        T::func = trampoline.write_call<5>(src, T::thunk);
    }

    template <class T, size_t idx, class U>
    void write_vfunc()
    {
        REL::Relocation<uintptr_t> vtbl { T::VTABLE[0] };
        U::func = vtbl.write_vfunc(idx, U::thunk);
    }

    template<size_t idx, class U>
    void write_vfunc(REL::VariantID id)
    {
        REL::Relocation<uintptr_t> vtbl { id };
        U::func = vtbl.write_vfunc(idx, U::thunk);
    }
}
