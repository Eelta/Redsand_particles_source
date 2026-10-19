#include "forms.hpp"
#include "hooks.hpp"
#include "papyrus.hpp"
#include "block_handler.hpp"
#include "settings.hpp"
#include "time_vfx.hpp"

static void SKSEMessage(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kPostLoad:
        case SKSE::MessagingInterface::kPostPostLoad:
            BlockHandler::Singleton().RequestModAPIs();
            BlockHandler::Singleton().TryRegisterPrecisionCallback();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            BlockHandler::Singleton().Reset();
            Forms::Update();
            HellbladeTime::VFX::Install();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            BlockHandler::Singleton().Reset();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            Settings::ReadSettings();
            Forms::Load();
            BlockHandler::Singleton().RegisterAttackEvents();
            HellbladeTime::VFX::Install();
            logger::info("Hellblade Player Time Independence: original block/time behavior retained; optional visual-only player block notification; attackEnabled={} legacyScreenLightning={}", Settings::attack.enabled, Settings::visual.enabled);
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
#ifndef NDEBUG
    const auto level = spdlog::level::trace;
    auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
    const auto level = spdlog::level::trace;
    auto logPath = logger::log_directory();
    *logPath /= fmt::format("{}.log"sv, SKSE::PluginDeclaration::GetSingleton()->GetName());

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(level);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);

    SKSE::Init(a_skse);
    Hooks::Install();
    Papyrus::Register();

    if (const auto api = SKSE::GetMessagingInterface())
        api->RegisterListener(SKSEMessage);

    return true;
}
