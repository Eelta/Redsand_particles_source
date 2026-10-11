#include "RedsandSettings.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace RedsandSettings
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr auto saveInterval = std::chrono::milliseconds(250);
		struct State
		{
			std::mutex valuesLock;
			std::mutex fileLock;
			Config config;
			std::filesystem::path path = "Data/SKSE/Plugins/RedsandParticles.ini";
			std::uint64_t revision = 0;
			std::uint64_t savedRevision = 0;
			Clock::time_point lastSaveAttempt{};
			bool saveFailed = false;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		Config Normalize(Config config)
		{
			auto valid = [](float value, float maximum, float minimum = 0.0f, float fallback = 1.0f) {
				return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
			};
			if (config.language != Language::English && config.language != Language::Chinese) { config.language = Language::English; }
			config.swordBrightness = valid(config.swordBrightness, maxBrightness);
			config.swordDensity = valid(config.swordDensity, maxDensity);
			config.swordSpread = valid(config.swordSpread, maxSpread, 0.0f, defaultSpread);
			config.swordSize = valid(config.swordSize, maxSize, minSize);
			config.bowBrightness = valid(config.bowBrightness, maxBrightness);
			config.bowDensity = valid(config.bowDensity, maxDensity);
			return config;
		}

		std::string_view Trim(std::string_view text)
		{
			constexpr auto whitespace = " \t\r\n";
			const auto begin = text.find_first_not_of(whitespace);
			if (begin == text.npos) { return {}; }
			return text.substr(begin, text.find_last_not_of(whitespace) - begin + 1);
		}

		std::string Lower(std::string_view text)
		{
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
				return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
			});
			return result;
		}

		void ReadBool(std::string_view value, bool& output)
		{
			const auto lower = Lower(value);
			if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") { output = true; }
			else if (lower == "false" || lower == "0" || lower == "no" || lower == "off") { output = false; }
		}

		void ReadFloat(std::string_view value, float& output)
		{
			if (value.empty()) { return; }
			float result = 1.0f;
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
			if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && std::isfinite(result)) { output = result; }
		}

		Config Read(std::istream& input)
		{
			Config result;
			std::string section, line;
			while (std::getline(input, line)) {
				auto text = Trim(line);
				if (text.starts_with("\xEF\xBB\xBF")) { text.remove_prefix(3); }
				text = Trim(text.substr(0, text.find_first_of(";#")));
				if (text.empty()) { continue; }
				if (text.front() == '[' && text.back() == ']') {
					section = Lower(Trim(text.substr(1, text.size() - 2)));
					continue;
				}
				const auto equal = text.find('=');
				if (equal == text.npos) { continue; }
				const auto key = Lower(Trim(text.substr(0, equal)));
				const auto value = Trim(text.substr(equal + 1));
				if (section == "menu") {
					if (key == "language") {
						const auto language = Lower(value);
						if (language == "english") { result.language = Language::English; }
						else if (language == "chinese") { result.language = Language::Chinese; }
					}
					else if (key == "diagnosticlogging") { ReadBool(value, result.diagnostics); }
				} else if (section == "swordparticles") {
					if (key == "enabled") { ReadBool(value, result.swordEnabled); }
					else if (key == "player") { ReadBool(value, result.swordPlayer); }
					else if (key == "npc") { ReadBool(value, result.swordNPC); }
					else if (key == "tip") { ReadBool(value, result.swordTip); }
					else if (key == "brightness") { ReadFloat(value, result.swordBrightness); }
					else if (key == "density") { ReadFloat(value, result.swordDensity); }
					else if (key == "spread") { ReadFloat(value, result.swordSpread); }
					else if (key == "size") { ReadFloat(value, result.swordSize); }
				} else if (section == "bowparticles") {
					if (key == "enabled") { ReadBool(value, result.bowEnabled); }
					else if (key == "player") { ReadBool(value, result.bowPlayer); }
					else if (key == "npc") { ReadBool(value, result.bowNPC); }
					else if (key == "brightness") { ReadFloat(value, result.bowBrightness); }
					else if (key == "density") { ReadFloat(value, result.bowDensity); }
					else if (key == "gathering") { ReadBool(value, result.bowGathering); }
					else if (key == "rings") { ReadBool(value, result.bowRings); }
					else if (key == "trails") { ReadBool(value, result.bowTrails); }
				}
			}
			return Normalize(result);
		}

		bool WriteAtomic(const std::filesystem::path& path, const Config& config)
		{
			std::error_code error;
			if (!path.parent_path().empty()) {
				std::filesystem::create_directories(path.parent_path(), error);
				if (error) { return false; }
			}
			auto temporary = path;
			temporary += ".tmp";
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output) { return false; }
				output.imbue(std::locale::classic());
				output << std::boolalpha << std::setprecision(6)
					<< "; Redsand Particles: menu changes take effect immediately.\n"
					<< "; Brightness: 0..5. Density: 0..2. 1 is the default multiplier.\n"
					<< "; Melee spread: 0..2 (default 1). Particle size: 0.25..2 (default 1).\n"
					<< "[Menu]\nLanguage=" << (config.language == Language::Chinese ? "Chinese" : "English") << '\n'
					<< "DiagnosticLogging=" << config.diagnostics << "\n\n"
					<< "[SwordParticles]\n"
					<< "Enabled=" << config.swordEnabled << '\n'
					<< "Player=" << config.swordPlayer << '\n'
					<< "NPC=" << config.swordNPC << '\n'
					<< "Tip=" << config.swordTip << '\n'
					<< "Brightness=" << config.swordBrightness << '\n'
					<< "Density=" << config.swordDensity << '\n'
					<< "Spread=" << config.swordSpread << '\n'
					<< "Size=" << config.swordSize << "\n\n"
					<< "[BowParticles]\n"
					<< "Enabled=" << config.bowEnabled << '\n'
					<< "Player=" << config.bowPlayer << '\n'
					<< "NPC=" << config.bowNPC << '\n'
					<< "Brightness=" << config.bowBrightness << '\n'
					<< "Density=" << config.bowDensity << '\n'
					<< "Gathering=" << config.bowGathering << '\n'
					<< "Rings=" << config.bowRings << '\n'
					<< "Trails=" << config.bowTrails << '\n';
				output.flush();
				if (!output) { return false; }
				output.close();
				if (!output) { return false; }
			}
#ifdef _WIN32

			return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
			std::filesystem::rename(temporary, path, error);
			return !error;
#endif
		}

		void Save(bool force)
		{
			auto& state = GetState();
			std::unique_lock fileLock(state.fileLock, std::defer_lock);
			if (force) { fileLock.lock(); }
			else if (!fileLock.try_lock()) { return; }
			Config snapshot;
			std::filesystem::path path;
			std::uint64_t revision;
			{
				std::lock_guard valuesLock(state.valuesLock);
				if (state.revision == state.savedRevision) { return; }
				const auto now = Clock::now();
				if (!force && now - state.lastSaveAttempt < saveInterval) { return; }
				state.lastSaveAttempt = now;
				snapshot = state.config;
				path = state.path;
				revision = state.revision;
			}
			const bool saved = WriteAtomic(path, snapshot);
			std::lock_guard valuesLock(state.valuesLock);
			state.saveFailed = !saved;
			if (saved) { state.savedRevision = revision; }

		}
	}

	Config Get()
	{
		auto& state = GetState();
		std::lock_guard lock(state.valuesLock);
		return state.config;
	}

	void Set(Config config)
	{
		config = Normalize(config);
		auto& state = GetState();
		std::lock_guard lock(state.valuesLock);
		if (config == state.config) { return; }
		state.config = config;
		++state.revision;
	}

	void Load(const std::filesystem::path& path)
	{
		auto& state = GetState();
		std::lock_guard fileLock(state.fileLock);
		std::ifstream input(path, std::ios::binary);
		const auto config = input ? Read(input) : Config{};
		std::lock_guard valuesLock(state.valuesLock);
		state.path = path;
		state.config = config;
		state.savedRevision = ++state.revision;
		state.lastSaveAttempt = {};
		state.saveFailed = false;
	}

	void TickPendingSave() { Save(false); }
	void FlushPendingSave() { Save(true); }
	void ResetDefaults()
	{
		auto config = Config{};
		config.language = Get().language;
		Set(config);
		FlushPendingSave();
	}
	bool SaveFailed()
	{
		auto& state = GetState();
		std::lock_guard lock(state.valuesLock);
		return state.saveFailed;
	}
}
