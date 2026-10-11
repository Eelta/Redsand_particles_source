#include "../source/RedsandSettings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}

	struct Workspace
	{
		std::filesystem::path path = std::filesystem::temp_directory_path() /
			("redsand-settings-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Workspace() { std::filesystem::create_directory(path); }
		~Workspace()
		{
			std::error_code ignored;
			if (path.parent_path() == std::filesystem::temp_directory_path() &&
				path.filename().string().starts_with("redsand-settings-tests-")) {
				std::filesystem::remove_all(path, ignored);
			}
		}
	};

	void Write(const std::filesystem::path& path, const std::string& content)
	{
		std::ofstream output(path, std::ios::binary);
		output << content;
		Check(static_cast<bool>(output), "could not write test fixture");
	}

	std::string Read(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	void ExistingBowIniRemainsCompatible(const Workspace& workspace)
	{
		const auto path = workspace.path / "legacy.ini";
		Write(path, "[BowParticles]\nEnabled=false\n");
		RedsandSettings::Load(path);
		auto expected = RedsandSettings::Config{};
		expected.bowEnabled = false;
		Check(RedsandSettings::Get() == expected, "legacy bow option or missing-key defaults changed");
	}

	void SwordDefaultsAndSavedOverridesRemainDistinct(const Workspace& workspace)
	{
		const auto path = workspace.path / "sword-defaults.ini";
		RedsandSettings::Load(path);
		const auto defaults = RedsandSettings::Get();
		Check(defaults.swordTip && defaults.swordBrightness == 1.0f && defaults.swordDensity == 1.0f &&
			defaults.swordSpread == 1.0f && defaults.swordSize == 1.0f,
			"new melee defaults must enable sword-tip attachment and use unit multipliers");
		Write(path, "[Menu]\nLanguage=Chinese\n[SwordParticles]\nTip=false\nBrightness=2\nDensity=0.5\nSpread=0.25\nSize=0.75\n");
		RedsandSettings::Load(path);
		const auto saved = RedsandSettings::Get();
		Check(!saved.swordTip && saved.swordBrightness == 2.0f && saved.swordDensity == 0.5f &&
			saved.swordSpread == 0.25f && saved.swordSize == 0.75f,
			"new melee defaults replaced explicitly saved values");
		RedsandSettings::ResetDefaults();
		RedsandSettings::Load(path);
		auto restored = defaults;
		restored.language = RedsandSettings::Language::Chinese;
		Check(RedsandSettings::Get() == restored, "Defaults did not restore sword-tip attachment and unit multipliers");
	}

	void OldIniPreservesSavedOptionsAndIgnoresRemovedStaffSection(const Workspace& workspace)
	{
		const auto path = workspace.path / "old-menu.ini";
		Write(path, "[SwordParticles]\nTip=false\nBrightness=2\n[BowParticles]\nTrails=false\nDensity=0.5\n"
			"[StaffParticles]\nEnabled=false\nPlayer=false\nNPC=false\nBrightness=4\nDensity=0\nGathering=false\nRings=false\nTrails=false\n");
		RedsandSettings::Load(path);
		auto expected = RedsandSettings::Config{};
		expected.swordTip = false;
		expected.swordBrightness = 2.0f;
		expected.bowTrails = false;
		expected.bowDensity = 0.5f;
		Check(RedsandSettings::Get() == expected, "old INI controls, English default or new melee defaults were not preserved");
		Check(expected.language == RedsandSettings::Language::English && expected.swordSpread == 1.0f && expected.swordSize == 1.0f,
			"new default language, spread or size changed");
		expected.swordDensity = 0.5f;
		RedsandSettings::Set(expected);
		RedsandSettings::FlushPendingSave();
		Check(Read(path).find("StaffParticles") == std::string::npos, "removed staff section was written back");
	}

	void LanguageSelectionPersistsAndRejectsUnknownValues(const Workspace& workspace)
	{
		const auto path = workspace.path / "language.ini";
		Write(path, "[Menu]\nLanguage=CHInese\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().language == RedsandSettings::Language::Chinese, "Chinese language did not load case-insensitively");
		auto config = RedsandSettings::Get();
		config.language = RedsandSettings::Language::English;
		RedsandSettings::Set(config);
		RedsandSettings::FlushPendingSave();
		Check(Read(path).find("Language=English") != std::string::npos, "English language choice did not persist");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().language == RedsandSettings::Language::English, "English language choice failed the round trip");
		Write(path, "[Menu]\nLanguage=French\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().language == RedsandSettings::Language::English, "unknown INI language did not keep English default");
		config = RedsandSettings::Get();
		config.language = static_cast<RedsandSettings::Language>(99);
		RedsandSettings::Set(config);
		Check(RedsandSettings::Get().language == RedsandSettings::Language::English, "invalid runtime language was published");
	}

	void IniInputCannotPublishInvalidMultipliers(const Workspace& workspace)
	{
		const auto path = workspace.path / "bounds.ini";
		Write(path,
			"\xEF\xBB\xBF[SWORdParticles]\r\n"
			"Tip = TrUe ; inline comment\nPlayer = OFF\nNPC = invalid\n"
			"Brightness = -3\nDensity = 900\nSpread=900\nSize=-2\n"
			"[BowParticles]\nEnabled=0\nNPC=yes\nBrightness=nan\nDensity=1.5junk\n"
			"Gathering = no\nRings = 1\nTrails = false # comment\n"
			"[StAffParticles]\nEnabled=off\nPlayer=0\nNPC=yes\nBrightness=9\nDensity=-2\n"
			"Gathering=false\nRings=no\nTrails=on\n");
		RedsandSettings::Load(path);
		const auto config = RedsandSettings::Get();
		Check(config.swordTip && !config.swordPlayer && config.swordNPC, "INI boolean parsing failed");
		Check(config.swordBrightness == 0.0f && config.swordDensity == RedsandSettings::maxDensity, "INI ranges were not bounded");
		Check(config.bowBrightness == 1.0f && config.bowDensity == 1.0f, "invalid numeric INI input was accepted");
		Check(!config.bowEnabled && config.bowNPC && !config.bowGathering && config.bowRings && !config.bowTrails, "bow controls did not load independently");
		Check(config.swordSpread == RedsandSettings::maxSpread && config.swordSize == RedsandSettings::minSize,
			"new melee INI ranges were not bounded");

		auto invalid = config;
		invalid.swordBrightness = std::numeric_limits<float>::quiet_NaN();
		invalid.swordDensity = std::numeric_limits<float>::infinity();
		invalid.bowBrightness = 1000.0f;
		invalid.bowDensity = -1.0f;
		invalid.swordSpread = std::numeric_limits<float>::infinity();
		invalid.swordSize = std::numeric_limits<float>::quiet_NaN();
		RedsandSettings::Set(invalid);
		const auto normalized = RedsandSettings::Get();
		Check(normalized.swordBrightness == 1.0f && normalized.swordDensity == 1.0f &&
			normalized.bowBrightness == RedsandSettings::maxBrightness && normalized.bowDensity == 0.0f &&
			normalized.swordSpread == RedsandSettings::defaultSpread && normalized.swordSize == 1.0f,
			"menu publication admitted non-finite or out-of-range values");
		invalid = normalized;
		invalid.swordSpread = -1.0f;
		invalid.swordSize = 90.0f;
		RedsandSettings::Set(invalid);
		Check(RedsandSettings::Get().swordSpread == 0.0f && RedsandSettings::Get().swordSize == RedsandSettings::maxSize,
			"runtime spread and size boundaries were not enforced");
		Write(path, "[SwordParticles]\nBrightness=\nDensity=\nSpread=nan\nSize=1.5junk\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().swordSpread == 1.0f && RedsandSettings::Get().swordSize == 1.0f &&
			RedsandSettings::Get().swordBrightness == 1.0f && RedsandSettings::Get().swordDensity == 1.0f,
			"invalid new melee values did not keep safe defaults");
	}

	void DiagnosticLoggingDefaultsOffAndTogglesImmediately(const Workspace& workspace)
	{
		const auto path = workspace.path / "diagnostics.ini";
		RedsandSettings::Load(path);
		Check(!RedsandSettings::Get().diagnostics, "diagnostic logging is enabled by default");
		auto config = RedsandSettings::Get();
		config.diagnostics = true;
		RedsandSettings::Set(config);
		Check(RedsandSettings::Get().diagnostics, "diagnostic logging did not enable immediately");
		RedsandSettings::FlushPendingSave();
		Check(Read(path).find("DiagnosticLogging=true") != std::string::npos, "diagnostic logging choice was not saved");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().diagnostics, "enabled diagnostic logging did not survive reload");
		config = RedsandSettings::Get();
		config.diagnostics = false;
		RedsandSettings::Set(config);
		Check(!RedsandSettings::Get().diagnostics, "diagnostic logging did not disable immediately");
		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(path);
		Check(!RedsandSettings::Get().diagnostics, "disabled diagnostic logging did not survive reload");
		Write(path, "[MENU]\nLanguage=Chinese\nDIAGNOSTICLogging=ON\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().diagnostics, "diagnostic logging did not parse case-insensitively");
		RedsandSettings::ResetDefaults();
		RedsandSettings::Load(path);
		Check(!RedsandSettings::Get().diagnostics && RedsandSettings::Get().language == RedsandSettings::Language::Chinese,
			"Defaults did not turn diagnostics off while preserving the language");
		Write(path, "[Menu]\nDiagnosticLogging=invalid\n");
		RedsandSettings::Load(path);
		Check(!RedsandSettings::Get().diagnostics, "invalid diagnostic logging value did not keep the safe default");
	}

	void ImmediatePublicationAndPersistenceRoundTrip(const Workspace& workspace)
	{
		const auto path = workspace.path / "nested" / "RedsandParticles.ini";
		RedsandSettings::Load(path);
		auto expected = RedsandSettings::Config{};
		expected.language = RedsandSettings::Language::Chinese;
		expected.diagnostics = true;
		expected.swordEnabled = false;
		expected.swordPlayer = false;
		expected.swordNPC = false;
		expected.swordTip = true;
		expected.swordBrightness = 2.5f;
		expected.swordDensity = 0.75f;
		expected.swordSpread = 1.25f;
		expected.swordSize = 1.5f;
		expected.bowEnabled = false;
		expected.bowPlayer = false;
		expected.bowNPC = false;
		expected.bowBrightness = 3.5f;
		expected.bowDensity = 1.25f;
		expected.bowGathering = false;
		expected.bowRings = false;
		expected.bowTrails = false;
		RedsandSettings::Set(expected);
		Check(RedsandSettings::Get() == expected, "menu changes require a save before they become visible");
		RedsandSettings::FlushPendingSave();
		Check(std::filesystem::is_regular_file(path), "settings were not saved in the target directory");
		Check(!std::filesystem::exists(path.string() + ".tmp"), "completed atomic replacement left a temporary file");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get() == expected, "one or more menu controls failed the save/load round-trip");
		Check(Read(path).find("Language=Chinese") != std::string::npos, "Chinese language choice did not persist");
		RedsandSettings::ResetDefaults();
		RedsandSettings::Load(path);
		auto defaults = RedsandSettings::Config{};
		defaults.language = RedsandSettings::Language::Chinese;
		Check(RedsandSettings::Get() == defaults, "Defaults failed to persist particle defaults while preserving the language");
	}

	void ControlReleaseFlushesTheLastSliderValue(const Workspace& workspace)
	{
		const auto path = workspace.path / "slider.ini";
		RedsandSettings::Load(path);
		auto config = RedsandSettings::Get();
		config.swordDensity = 0.5f;
		RedsandSettings::Set(config);
		RedsandSettings::TickPendingSave();
		Check(Read(path).find("Density=0.5") != std::string::npos, "the first automatic save was lost");
		config.swordDensity = 1.75f;
		RedsandSettings::Set(config);

		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().swordDensity == 1.75f, "the final dragged slider value was not persisted");
	}

	void FailedSaveCanRecoverWithoutAnotherMenuChange(const Workspace& workspace)
	{
		const auto blocker = workspace.path / "blocked-parent";
		Write(blocker, "not a directory");
		const auto path = blocker / "retry.ini";
		RedsandSettings::Load(path);
		auto config = RedsandSettings::Get();
		config.bowBrightness = 4.5f;
		RedsandSettings::Set(config);
		RedsandSettings::FlushPendingSave();
		Check(RedsandSettings::Get() == config, "failed persistence rolled back the live effect");
		Check(RedsandSettings::SaveFailed(), "an actual save failure was not reported to the menu");
		std::filesystem::remove(blocker);
		std::filesystem::create_directory(blocker);
		RedsandSettings::FlushPendingSave();
		Check(!RedsandSettings::SaveFailed(), "successful retry did not clear the menu's save failure status");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get() == config, "failed persistence incorrectly cleared the pending revision");
	}

	void GameAndMenuThreadsObserveCompleteSnapshots(const Workspace& workspace)
	{
		RedsandSettings::Load(workspace.path / "concurrent.ini");
		auto first = RedsandSettings::Config{};
		first.swordTip = true;
		first.swordBrightness = 0.25f;
		first.bowBrightness = 4.5f;
		first.language = RedsandSettings::Language::Chinese;
		first.diagnostics = true;
		first.swordSpread = 0.5f;
		first.swordSize = 1.75f;
		auto second = RedsandSettings::Config{};
		second.swordNPC = false;
		second.swordBrightness = 4.75f;
		second.bowBrightness = 0.5f;
		second.swordSpread = 1.5f;
		second.swordSize = 0.25f;
		RedsandSettings::Set(first);
		std::atomic_bool done = false, invalid = false;
		auto read = [&] {
			while (!done.load()) {
				const auto config = RedsandSettings::Get();
				if (config != first && config != second) { invalid.store(true); }
			}
		};
		std::thread game(read), renderer(read);
		for (unsigned iteration = 0; iteration < 20000; ++iteration) {
			RedsandSettings::Set(iteration % 2 == 0 ? first : second);
		}
		done.store(true);
		game.join();
		renderer.join();
		Check(!invalid.load(), "a concurrent reader received a partially published configuration");
		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(workspace.path / "concurrent.ini");
		Check(RedsandSettings::Get() == second, "the last concurrent publication was not saved");
	}
}

int main()
{
	try {
		const Workspace workspace;
		ExistingBowIniRemainsCompatible(workspace);
		SwordDefaultsAndSavedOverridesRemainDistinct(workspace);
		OldIniPreservesSavedOptionsAndIgnoresRemovedStaffSection(workspace);
		LanguageSelectionPersistsAndRejectsUnknownValues(workspace);
		IniInputCannotPublishInvalidMultipliers(workspace);
		DiagnosticLoggingDefaultsOffAndTogglesImmediately(workspace);
		ImmediatePublicationAndPersistenceRoundTrip(workspace);
		ControlReleaseFlushesTheLastSliderValue(workspace);
		FailedSaveCanRecoverWithoutAnotherMenuChange(workspace);
		GameAndMenuThreadsObserveCompleteSnapshots(workspace);
		std::cout << "Runtime settings, persistence and thread snapshot checks passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
