#include "../source/RedsandLog.h"
#include "../source/RedsandSettings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}

	struct Workspace
	{
		std::filesystem::path path = std::filesystem::temp_directory_path() /
			("redsand-logging-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Workspace() { std::filesystem::create_directory(path); }
		~Workspace()
		{
			RedsandLog::Initialize();
			std::error_code ignored;
			if (path.parent_path() == std::filesystem::temp_directory_path() &&
				path.filename().string().starts_with("redsand-logging-tests-")) {
				std::filesystem::remove_all(path, ignored);
			}
		}
	};

	std::string Read(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	void Enable(bool enabled)
	{
		auto config = RedsandSettings::Get();
		config.diagnostics = enabled;
		RedsandSettings::Set(config);
		RedsandLog::Sync();
	}

	void OffCreatesNothingAndNeverTouchesPrecision(const Workspace& workspace)
	{
		const auto directory = workspace.path / "default-off";
		RedsandSettings::Load(workspace.path / "missing.ini");
		RedsandLog::Initialize(directory);
		Check(!RedsandLog::Enabled(), "diagnostics did not default off");
		RedsandLog::Write(RedsandLog::Level::Info, "disabled message");
		Check(!std::filesystem::exists(directory), "disabled logging created a file or directory");
		std::filesystem::create_directory(directory);
		const auto precision = directory / "Precision.log";
		{ std::ofstream output(precision); output << "Precision owns this file\n"; }
		{ std::ofstream output(directory / "RedsandParticles.log", std::ios::binary); output << "previous game session\n"; }
		const auto original = Read(precision);
		RedsandLog::Write(RedsandLog::Level::Info, "still disabled");
		Check(Read(directory / "RedsandParticles.log") == "previous game session\n", "disabled logging overwrote its previous session");
		Enable(true);
		RedsandLog::Write(RedsandLog::Level::Info, "first enabled message");
		const auto first = Read(directory / "RedsandParticles.log");
		Check(first.find("[info] first enabled message\n") != std::string::npos, "enabled message was not flushed to the dedicated log");
		Check(first.find("previous game session") == std::string::npos, "first enabled write appended to the previous game session");
		Enable(false);
		RedsandLog::Write(RedsandLog::Level::Warning, "disabled warning");
		Check(Read(directory / "RedsandParticles.log") == first, "disabled diagnostics appended to the log");
		Enable(true);
		RedsandLog::Write(RedsandLog::Level::Warning, "second enabled message");
		const auto second = Read(directory / "RedsandParticles.log");
		Check(second.starts_with(first) && second.find("[warning] second enabled message\n") != std::string::npos,
			"reenabling diagnostics truncated the previous session or lost a warning");
		RedsandLog::Initialize(directory);
		RedsandLog::Write(RedsandLog::Level::Info, "reinitialized session");
		Check(Read(directory / "RedsandParticles.log").starts_with(second), "reinitializing logging truncated its file");
		Check(Read(precision) == original, "dedicated logging modified Precision.log");
	}

	void ConcurrentWritesStayWholeAndComplete(const Workspace& workspace)
	{
		const auto directory = workspace.path / "concurrent";
		RedsandLog::Initialize(directory);
		Enable(true);
		std::vector<std::thread> threads;
		for (int thread = 0; thread < 8; ++thread) {
			threads.emplace_back([thread] {
				for (int line = 0; line < 200; ++line) {
					RedsandLog::Write(RedsandLog::Level::Info, "thread=" + std::to_string(thread) + " line=" + std::to_string(line));
				}
			});
		}
		for (auto& thread : threads) { thread.join(); }
		Enable(false);
		std::set<std::string> messages;
		std::ifstream input(directory / "RedsandParticles.log");
		std::string line;
		while (std::getline(input, line)) {
			const auto prefix = line.find("] [info] ");
			Check(prefix != std::string::npos, "concurrent writes interleaved a line prefix");
			Check(messages.insert(line.substr(prefix + 9)).second, "concurrent writes duplicated a message");
		}
		Check(messages.size() == 1600, "concurrent logging dropped or joined messages");
		for (int thread = 0; thread < 8; ++thread) {
			for (int index = 0; index < 200; ++index) {
				Check(messages.contains("thread=" + std::to_string(thread) + " line=" + std::to_string(index)),
					"concurrent logging corrupted a message");
			}
		}
	}

	void InvalidDestinationFailsSafelyAndRecovers(const Workspace& workspace)
	{
		const auto directory = workspace.path / "blocked";
		{ std::ofstream output(directory); output << "destination is a file"; }
		RedsandLog::Initialize(directory);
		Enable(true);
		RedsandLog::Write(RedsandLog::Level::Warning, "cannot be written");
		Check(Read(directory) == "destination is a file", "failed log creation changed its invalid destination");
		Check(!std::filesystem::exists(workspace.path / "RedsandParticles.log"), "failed logger fell back to its parent directory");
		std::filesystem::remove(directory);
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		RedsandLog::Write(RedsandLog::Level::Info, "recovered destination");
		Check(Read(directory / "RedsandParticles.log").find("recovered destination") != std::string::npos,
			"logger did not retry and recover after its destination was repaired");
		Enable(false);
	}

	void NativePathsAndIniToggleWork(const Workspace& workspace)
	{
		const auto directory = workspace.path / std::filesystem::path(L"\u7ea2\u6c99\u7c92\u5b50");
		const auto ini = workspace.path / "toggle.ini";
		RedsandLog::Initialize(directory);
		{ std::ofstream output(ini); output << "[Menu]\nDiagnosticLogging=true\n"; }
		RedsandSettings::Load(ini);
		RedsandLog::Write(RedsandLog::Level::Info, "native Unicode destination");
		const auto enabled = Read(directory / "RedsandParticles.log");
		Check(enabled.find("native Unicode destination") != std::string::npos, "native filesystem path did not support Unicode");
		{ std::ofstream output(ini); output << "[Menu]\nDiagnosticLogging=false\n"; }
		RedsandSettings::Load(ini);
		RedsandLog::Write(RedsandLog::Level::Info, "disabled by INI");
		Check(Read(directory / "RedsandParticles.log") == enabled, "INI disable did not immediately stop logging");
		{ std::ofstream output(ini); output << "[Menu]\nDiagnosticLogging=true\n"; }
		RedsandSettings::Load(ini);
		RedsandLog::Write(RedsandLog::Level::Info, "immediately reenabled by INI");
		const auto reenabled = Read(directory / "RedsandParticles.log");
		Check(reenabled.starts_with(enabled) && reenabled.find("immediately reenabled by INI") != std::string::npos,
			"direct INI reenable was delayed or truncated previous diagnostics");
	}

	void FileAndMessageSizesStayBounded(const Workspace& workspace)
	{
		const auto directory = workspace.path / "bounded";
		const auto path = directory / "RedsandParticles.log";
		RedsandLog::Initialize(directory);
		Enable(true);
		RedsandLog::Write(RedsandLog::Level::Info, std::string(RedsandLog::maxFileBytes * 2, 'x'));
		const auto oversized = Read(path);
		Check(oversized.size() <= RedsandLog::maxMessageBytes + 64 && oversized.ends_with(" [truncated]\n"),
			"oversized message exceeded its bound or lacked a truncation marker");
		RedsandLog::Write(RedsandLog::Level::Info, std::string(RedsandLog::maxMessageBytes - 1, 'y') + "\xE4\xB8\xAD" + "z");
		const auto unicode = Read(path).substr(oversized.size());
		Check(unicode.ends_with("y [truncated]\n") && unicode.find('\xE4') == std::string::npos,
			"message truncation split a UTF-8 character");
		RedsandLog::Write(RedsandLog::Level::Info, "before bounded overwrite");
		const std::string payload(RedsandLog::maxMessageBytes - 32, 'b');
		for (int index = 0; index < 600; ++index) {
			RedsandLog::Write(RedsandLog::Level::Info, "record=" + std::to_string(index) + " " + payload);
			Check(std::filesystem::file_size(path) <= RedsandLog::maxFileBytes, "diagnostic file exceeded 8 MiB");
		}
		RedsandLog::Write(RedsandLog::Level::Warning, "after bounded overwrite");
		const auto bounded = Read(path);
		Check(bounded.find("before bounded overwrite") == std::string::npos && bounded.ends_with("[warning] after bounded overwrite\n"),
			"full log did not overwrite its old records and continue writing");
		Check(bounded.starts_with("[") && bounded.find("record=599 ") != std::string::npos, "bounded overwrite lost the latest complete records");
		Enable(false);
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			Check(entry.path().filename() == "RedsandParticles.log", "bounded logging created an extra rotated file");
		}
	}

	void ConcurrentBoundedOverwriteKeepsWholeLines(const Workspace& workspace)
	{
		const auto directory = workspace.path / "concurrent-bounded";
		const auto path = directory / "RedsandParticles.log";
		RedsandLog::Initialize(directory);
		Enable(true);
		const std::string payload(RedsandLog::maxMessageBytes - 64, 'c');
		std::vector<std::thread> threads;
		for (int thread = 0; thread < 8; ++thread) {
			threads.emplace_back([thread, &payload] {
				for (int index = 0; index < 100; ++index) {
					RedsandLog::Write(RedsandLog::Level::Info, "thread=" + std::to_string(thread) + " line=" + std::to_string(index) + " " + payload);
				}
			});
		}
		for (auto& thread : threads) { thread.join(); }
		Enable(false);
		Check(std::filesystem::file_size(path) <= RedsandLog::maxFileBytes, "concurrent overwrite exceeded its file bound");
		std::ifstream input(path);
		std::string line;
		std::set<std::string> identities;
		while (std::getline(input, line)) {
			const auto start = line.find("] [info] thread=");
			Check(start != std::string::npos && line.starts_with("["), "concurrent overwrite split a line prefix");
			const auto content = line.find(" " + payload, start);
			Check(content != std::string::npos && line.size() == content + 1 + payload.size(), "concurrent overwrite split or joined a payload");
			Check(identities.insert(line.substr(start + 9, content - start - 9)).second, "concurrent overwrite duplicated a retained record");
		}
		Check(!identities.empty() && identities.size() < 800, "concurrent test did not exercise bounded overwrite");
	}
}

int main()
{
	try {
		const Workspace workspace;
		OffCreatesNothingAndNeverTouchesPrecision(workspace);
		ConcurrentWritesStayWholeAndComplete(workspace);
		InvalidDestinationFailsSafelyAndRecovers(workspace);
		NativePathsAndIniToggleWork(workspace);
		FileAndMessageSizesStayBounded(workspace);
		ConcurrentBoundedOverwriteKeepsWholeLines(workspace);
		std::cout << "Independent logging regressions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
