#include "RedsandLog.h"
#include "RedsandSettings.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#ifndef REDSAND_LOG_STANDALONE
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#endif

namespace RedsandLog
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		struct State
		{
			std::mutex lock;
			std::filesystem::path directory;
			std::ofstream output;
			Clock::time_point nextAttempt{};
			std::uintmax_t bytes = 0;
			bool started = false, truncatePending = false;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		std::filesystem::path ResolveDirectory()
		{
#ifndef REDSAND_LOG_STANDALONE
			wchar_t* buffer = nullptr;
			const auto result = REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));
			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(buffer, REX::W32::CoTaskMemFree);
			if (!knownPath || result != 0) { return {}; }
			std::filesystem::path directory = knownPath.get();
			directory /= "My Games";
			directory /= std::filesystem::exists("steam_api64.dll") ?
				(std::filesystem::exists("openvr_api.dll") ? "Skyrim VR" : "Skyrim Special Edition") :
				"Skyrim Special Edition GOG";
			return directory / "SKSE";
#else
			return {};
#endif
		}

		void Close(State& state)
		{
			if (state.output.is_open()) { state.output.close(); }
			state.output.clear();
		}

		bool Open(State& state)
		{
			if (state.output.is_open()) { return true; }
			const auto now = Clock::now();
			if (now < state.nextAttempt) { return false; }
			state.nextAttempt = now + std::chrono::seconds(1);
			if (state.directory.empty()) { state.directory = ResolveDirectory(); }
			if (state.directory.empty()) { return false; }
			std::error_code error;
			std::filesystem::create_directories(state.directory, error);
			if (error) { return false; }
			state.output.clear();
			const bool truncate = !state.started || state.truncatePending;
			const auto path = state.directory / "RedsandParticles.log";
			state.output.open(path, std::ios::out | std::ios::binary | (truncate ? std::ios::trunc : std::ios::app));
			if (!state.output.is_open()) { return false; }
			state.bytes = truncate ? 0 : std::filesystem::file_size(path, error);
			if (error) { Close(state); return false; }
			state.started = true;
			state.truncatePending = false;
			state.nextAttempt = {};
			return true;
		}
	}

	bool Enabled() noexcept
	{
		try { return RedsandSettings::Get().diagnostics; } catch (...) { return false; }
	}

	void Initialize(const std::filesystem::path& directory) noexcept
	{
		try {
			auto& state = GetState();
			std::lock_guard guard(state.lock);
			Close(state);
			const auto nextDirectory = directory.empty() ? ResolveDirectory() : directory;
			if (state.directory != nextDirectory) {
				state.started = state.truncatePending = false;
				state.bytes = 0;
			}
			state.directory = nextDirectory;
			state.nextAttempt = {};
		} catch (...) {}
	}

	void Sync() noexcept
	{
		try {
			auto& state = GetState();
			std::lock_guard guard(state.lock);
			if (!Enabled()) {
				Close(state);
				state.nextAttempt = {};
			}
		} catch (...) {}
	}

	void Write(Level level, std::string_view message) noexcept
	{
		try {
			auto& state = GetState();
			std::lock_guard guard(state.lock);
			if (!Enabled()) {
				Close(state);
				state.nextAttempt = {};
				return;
			}
			if (!Open(state)) { return; }
			const auto now = std::chrono::system_clock::now();
			const auto time = std::chrono::system_clock::to_time_t(now);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &time);
#else
			localtime_r(&time, &local);
#endif
			char stamp[32]{};
			std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
			std::string line = "[" + std::string(stamp) + "] [" + (level == Level::Warning ? "warning" : "info") + "] ";
			const bool clipped = message.size() > maxMessageBytes;
			auto length = clipped ? maxMessageBytes : message.size();
			if (clipped) {
				while (length && (static_cast<unsigned char>(message[length]) & 0xC0) == 0x80) { --length; }
			}
			if (length) { line.append(message.data(), length); }
			if (clipped) { line += " [truncated]"; }
			line += '\n';
			if (state.bytes + line.size() > maxFileBytes) {
				Close(state);
				state.truncatePending = true;
				if (!Open(state)) { return; }
			}
			state.output.write(line.data(), static_cast<std::streamsize>(line.size()));
			state.output.flush();
			if (!state.output) { Close(state); state.nextAttempt = Clock::now() + std::chrono::seconds(1); }
			else { state.bytes += line.size(); }
		} catch (...) {}
	}
}
