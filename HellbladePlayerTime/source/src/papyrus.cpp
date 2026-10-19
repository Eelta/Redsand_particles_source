#include "papyrus.hpp"
#include "forms.hpp"
#include "settings.hpp"

namespace detail
{
    void config_close([[maybe_unused]] RE::TESQuest* quest) {
        Settings::ReadSettings();
        Forms::Update();
    }

    bool register_functions(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("OnConfigClose"sv, "Hellblade_MCM"sv, config_close);
        return true;
    }
}

void Papyrus::Register() {
    if (const auto api = SKSE::GetPapyrusInterface())
        api->Register(detail::register_functions);
}
