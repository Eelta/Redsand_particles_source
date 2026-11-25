#include "../source/RedsandSettings.h"

#include <atomic>
#include <barrier>
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
		Check(!defaults.swordReleasedTrail && defaults.swordRibbonLifetime == .2f && defaults.swordParticleLifetime == .3f &&
			defaults.swordRibbonWidth == .15f && defaults.swordTrailMomentum == 1.f && defaults.swordRibbonFadeOut == .25f &&
			defaults.swordParticleFadeOut == .4f && defaults.swordParticleGravity == .3f,
			"Nexus Redsand preset differs from the approved defaults");
		Check(defaults.swordTip && defaults.swordBrightness == 1.0f && defaults.swordDensity == 1.0f &&
			defaults.swordSpread == 1.0f && defaults.swordSize == 1.0f,
			"new melee defaults must enable sword-tip attachment and use unit multipliers");
		Write(path, "[Menu]\nLanguage=Chinese\n[SwordParticles]\nTip=false\nBrightness=2\nDensity=0.5\nSpread=0.25\nSize=0.75\nRibbonWidth=0.15\n");
		RedsandSettings::Load(path);
		const auto saved = RedsandSettings::Get();
		Check(!saved.swordTip && saved.swordBrightness == 2.0f && saved.swordDensity == 0.5f &&
			saved.swordSpread == 0.25f && saved.swordSize == 0.75f && saved.swordRibbonWidth == .15f,
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
		Write(path, "[SwordParticles]\nTip=false\nBrightness=2\nRibbonSmoothing=false\n[BowParticles]\nTrails=false\nDensity=0.5\n"
			"[StaffParticles]\nEnabled=false\nPlayer=false\nNPC=false\nBrightness=4\nDensity=0\nGathering=false\nRings=false\nTrails=false\n");
		RedsandSettings::Load(path);
		auto expected = RedsandSettings::Config{};
		expected.swordTip = false;
		expected.swordBrightness = 2.0f;
		expected.redsand.arcBrightness = expected.redsand.particleBrightness = 2.f;
		expected.bowTrails = false;
		expected.bowDensity = 0.5f;
		Check(RedsandSettings::Get() == expected, "old INI controls, English default or new melee defaults were not preserved");
		Check(expected.language == RedsandSettings::Language::English && expected.swordSpread == 1.0f && expected.swordSize == 1.0f,
			"new default language, spread or size changed");
		expected.swordDensity = 0.5f;
		expected.swordRibbonFadeOut=.3f;expected.swordParticleFadeOut=.6f;expected.swordParticleGravity=0.f;
		RedsandSettings::Set(expected);
		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get()==expected,"global fade/gravity settings or zero-gravity choice did not survive reload");
		expected.swordRibbonLifetime = .7f;
		expected.swordRibbonFadeOut = .9f;
		expected.swordParticleLifetime = .05f;
		expected.swordParticleFadeOut = .08f;
		RedsandSettings::Set(expected);
		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get() == expected, "a long arc forced short scatter times to grow during normalization or persistence");
		const auto categoryPath = workspace.path / "independent-category-timing.ini";
		Write(categoryPath, "[Enchantment.none]\nEnabled=true\nRibbonLifetime=0.7\nRibbonFadeOut=0.9\nParticleLifetime=0.05\nParticleFadeOut=0.08\n");
		RedsandSettings::Load(categoryPath);
		auto category = RedsandSettings::Get();
		RedsandSettings::ApplyMeleeAppearance(category, "none", "");
		Check(category.swordRibbonLifetime == .7f && category.swordRibbonFadeOut == .9f &&
			category.swordParticleLifetime == .05f && category.swordParticleFadeOut == .08f,
			"category-specific scatter times were coupled to the arc");
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
		expected.swordKeepPrecisionTrail = true;
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

	void PrecisionTrailIsOptInAndPersists(const Workspace& workspace)
	{
		const auto path = workspace.path / "precision-ribbon.ini";
		Write(path, "[SwordParticles]\nTip=true\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().swordKeepPrecisionTrail, "approved dual trails should be enabled by default");
		Write(path, "[SwordParticles]\nKeepPrecisionTrail=ON\n[BowParticles]\nTrails=false\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().swordKeepPrecisionTrail && !RedsandSettings::Get().bowTrails,
			"native melee and bow trail controls were not independent");
		auto config = RedsandSettings::Get();
		config.swordKeepPrecisionTrail = false;
		RedsandSettings::Set(config);
		Check(!RedsandSettings::Get().swordKeepPrecisionTrail, "native ribbon did not disable immediately");
		RedsandSettings::FlushPendingSave();
		Check(Read(path).find("KeepPrecisionTrail=false") != std::string::npos, "native ribbon preference did not save");
		Write(path, "[SwordParticles]\nKeepPrecisionTrail=invalid\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().swordKeepPrecisionTrail, "invalid native ribbon value lost the approved default");
	}

	void ImpactLevelsAndSwitchesPreserveExistingSettings(const Workspace& workspace)
	{
		const auto path = workspace.path / "impact.ini";
		Write(path, "[Menu]\nLanguage=Chinese\n[SwordParticles]\nKeepPrecisionTrail=true\nBrightness=2.5\n"
			"[BowParticles]\nDensity=0.75\n[ImpactVFX]\nEnabled=false\nHit=false\nSlash=false\nBlock=true\nPreserveValhallaRing=true\nBrightnessLevel=3\nDensityLevel=0\nSizeLevel=1\n");
		RedsandSettings::Load(path);
		auto config = RedsandSettings::Get();
		Check(!config.impactEnabled && !config.impactHit && !config.impactSlash && config.impactBlock &&
			config.impactBrightness == 12.f && config.impactDensity == .1f && config.impactSize == .5f,
			"impact settings failed to load");
		Check(config.hitBrightness == 12.f && config.hitDensity == .1f && config.hitSize == .5f && config.cutBrightness == 12.f,
			"split controls did not inherit the legacy appearance");
		Check(config.swordKeepPrecisionTrail && config.swordBrightness == 2.5f && config.bowDensity == 0.75f &&
			config.language == RedsandSettings::Language::Chinese, "impact migration changed prior settings");
		config.impactEnabled = true; config.impactBrightness = 1.37f; config.impactDensity = 2.85f; config.impactSize = .73f;
		config.hitBrightness = 0.f; config.hitDensity = 4.f; config.hitSize = 2.f; config.cutBrightness = .25f;
		RedsandSettings::Set(config); RedsandSettings::FlushPendingSave(); RedsandSettings::Load(path);
		Check(RedsandSettings::Get() == config, "impact settings did not persist independently");
		Write(path, "[ImpactVFX]\nHitBrightness=0\nCutBrightness=0.25\nBrightness=9\nHitDensity=nan\nDensity=0.4\nHitSize=25\nSize=0.5\n");
		RedsandSettings::Load(path);
		const auto overrides = RedsandSettings::Get();
		Check(overrides.hitBrightness == 0.f && overrides.cutBrightness == .25f && overrides.impactBrightness == 9.f &&
			overrides.hitDensity == .4f && overrides.hitSize == RedsandSettings::maxSize,
			"explicit split controls lost priority, fallback, or limits");
		Check(Read(path).find("PreserveValhallaRing") == std::string::npos, "removed combat integration setting was still saved");
		Write(path, "[ImpactVFX]\nBrightnessLevel=nan\nDensityLevel=1.5\nSizeLevel=junk\n");
		RedsandSettings::Load(path);
		Check(RedsandSettings::Get().impactBrightness == 1 && RedsandSettings::Get().impactDensity == .1f &&
			RedsandSettings::Get().impactSize == 1, "invalid levels did not retain preset defaults");
		config = RedsandSettings::Get(); config.impactBrightness = 99; config.impactDensity = -8;
		RedsandSettings::Set(config);
		Check(RedsandSettings::Get().impactBrightness == 20 && RedsandSettings::Get().impactDensity == 0, "levels were not clamped");
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
		std::barrier phase(2);
		std::atomic_bool stale = false;
		std::thread reader([&] {
			for (unsigned i = 0; i < 2000; ++i) {
				phase.arrive_and_wait();
				if (RedsandSettings::Get() != (i % 2 == 0 ? first : second)) { stale.store(true); }
				phase.arrive_and_wait();
			}
		});
		for (unsigned i = 0; i < 2000; ++i) {
			RedsandSettings::Set(i % 2 == 0 ? first : second);
			phase.arrive_and_wait();
			phase.arrive_and_wait();
		}
		reader.join();
		Check(!stale.load(), "a published menu change left a reader's cached settings stale");
	}
}

void RGBAndCutLengthRoundTrip(const Workspace& workspace)
{
	using namespace RedsandSettings;
	const auto path = workspace.path / "rgb.ini";
	Write(path, "[ImpactVFX]\nCutLength=3.25\nCutWidth=1.75\n[Color.Cut]\nEnabled=true\nR=0.15\nG=0.8\nB=0.4\n[Color.Melee]\nEnabled=true\nR=0\nG=0\nB=0\n");
	Load(path);
	auto config = Get();
	Check(config.cutLength == 3.25f && config.cutWidth == 1.75f, "cut length or width was not loaded");
	Check(config.colors[static_cast<unsigned>(ColorTarget::Cut)].rgb == std::array<float, 3>{ .15f, .8f, .4f }, "cut RGB was not loaded");
	Check(config.colors[0].custom && config.colors[0].rgb == std::array<float, 3>{ 0.f, 0.f, 0.f }, "black must be a valid custom RGB");
	config.cutLength = 100.f;
	config.cutWidth = 100.f;
	config.colors[2].rgb = { -1.f, 8.f, std::numeric_limits<float>::quiet_NaN() };
	Set(config); FlushPendingSave();
	const auto saved = Get();
	Check(saved.cutLength == maxCutLength && saved.cutWidth == maxCutWidth &&
		saved.colors[2].rgb == std::array<float, 3>{ 0.f, 1.f, defaultColors[2].rgb[2] }, "RGB/length/width normalization failed");
	Load(path); Check(Get() == saved, "RGB/length persistence changed values");
	config = Get(); config.cutLength = std::numeric_limits<float>::quiet_NaN(); config.colors[0].custom = false;
	Set(config); Check(Get().cutLength == 1.f && !Get().colors[0].custom, "disable custom or invalid length fallback failed");
}

void RandomSlashPersistsAndDefaultsOff(const Workspace& workspace)
{
    using namespace RedsandSettings;
    const auto path = workspace.path / "random-slash.ini";
    Write(path, "[ImpactVFX]\nRandomSlashColor=true\nRandomSlashValues=true\nRandomSlashRange=0.25\nBrightness=3.5\n");
    Load(path);
    Check(Get().slashRandomColor && Get().impactBrightness == 3.5f, "random slash INI was not loaded");
    Check(Get().slashRandomValues && Get().slashRandomRange == .25f, "numeric random toggle/range was not loaded");
    auto saved = Get(); saved.cutLength = 2.25f; Set(saved); FlushPendingSave();
    Check(Read(path).find("RandomSlashColor=true") != std::string::npos, "random slash was not serialized");
    Load(path); Check(Get() == saved, "random slash did not round trip");
    ResetDefaults(); FlushPendingSave(); Load(path);
    Check(!Get().slashRandomColor && Get().impactBrightness == 1.f, "random slash defaults must be off");
    Check(!Get().slashRandomValues && Get().slashRandomRange == .5f, "numeric random defaults must be off with 50% range");
    auto invalid = Get(); invalid.slashRandomRange = 9.f; Set(invalid);
    Check(Get().slashRandomRange == 1.f, "numeric range maximum was not clamped");
    invalid.slashRandomRange = -1.f; Set(invalid);
    Check(Get().slashRandomRange == 0.f, "numeric range minimum was not clamped");
    invalid.slashRandomRange = std::numeric_limits<float>::quiet_NaN(); Set(invalid);
    Check(Get().slashRandomRange == .5f, "invalid numeric range did not restore 50%");
}

void OldBlockStylesDoNotOverrideSparks(const Workspace& workspace)
{
	using namespace RedsandSettings;
	const auto path = workspace.path / "block-style.ini";
	Write(path, "[ImpactVFX]\nBlock=false\nBrightness=1.75\nDensity=0.6\n");
	Load(path);
	const auto baseline = Get();
	for (const auto* style : {"1", "2", "3", "4", "5", "-1", "invalid"}) {
		Write(path, "[ImpactVFX]\nBlock=false\nBrightness=1.75\nDensity=0.6\nBlockStyle=" + std::string(style) + "\n");
		Load(path);
		Check(Get() == baseline, "a legacy block style changed the sparks settings");
		auto config = Get();
		config.diagnostics = true;
		Set(config);
		FlushPendingSave();
		Load(path);
		Check(Get() == config && Read(path).find("BlockStyle=") == std::string::npos,
			"the retired block selector was written back or changed the sparks settings");
	}
}

int main()
{
	try {
		const Workspace workspace;
		const auto migration = workspace.path / "redsand-migration.ini";
		Write(migration, "[SwordParticles]\nEnabled=false\nReleasedTrail=true\nBrightness=2\nDensity=0.25\nSize=1.5\nSpread=3\nNPC=false\n"
			"ParticleLifetime=0.2\nParticleFadeOut=0.25\n[Enchantment.fire]\nEnabled=true\nReleasedTrail=true\n"
			"Brightness=4\nDensity=0.5\nSize=2\nSpread=2\n");
		RedsandSettings::Load(migration);
		auto migrated = RedsandSettings::Get();
		Check(migrated.redsand.arcBrightness == 2.f && migrated.redsand.particleBrightness == 2.f &&
			std::abs(migrated.redsand.density - .25f / 1.7f) < .00001f && migrated.redsand.size == 1.5f &&
			!migrated.redsand.npc && migrated.redsand.player && !migrated.swordEnabled, "legacy Redsand appearance did not migrate");
		auto category = migrated;
		RedsandSettings::ApplyMeleeAppearance(category, "fire", "");
		Check(category.redsand.arcBrightness == 4.f && std::abs(category.redsand.density - .5f / 1.35f) < .00001f &&
			category.redsand.size == 2.f, "legacy Redsand category did not migrate");
		migrated.redsand.arcBrightness = 0.f;
		migrated.redsand.particleBrightness = 3.f;
		migrated.redsand.size = 4.f;
		migrated.swordParticleWidth = 3.f;
		RedsandSettings::SetSwingDuration(migrated, .9f);
		RedsandSettings::Set(migrated);
		RedsandSettings::FlushPendingSave();
		RedsandSettings::Load(migration);
		const auto restored = RedsandSettings::Get();
		Check(restored.redsand == migrated.redsand && restored.swordSize == 1.5f && restored.swordBrightness == 2.f &&
			restored.swordParticleWidth == 3.f && std::abs(RedsandSettings::SwingDuration(restored) - .9f) < .001f,
			"Redsand settings did not stay independent after saving and loading");
		Write(migration, "[SwordParticles]\nTrailMomentum=1.75\nDensity=4\nSpread=3\n[RedsandTrail]\nSpacing=10\nDensity=0.36\n"
			"[Enchantment.fire]\nEnabled=true\nTrailMomentum=2.5\nRedsandDensity=10\nRedsandSpacing=0\n");
		RedsandSettings::Load(migration);
		migrated = RedsandSettings::Get();
		Check(std::abs(migrated.redsand.density - .36f / 4.15f) < .00001f && migrated.swordDensity == 4.f &&
			migrated.swordSpread == 3.f && migrated.swordTrailMomentum == 1.75f,
			"consolidating legacy spacing changed the dot rate, ordinary particles or saved speed");
		category = migrated;
		RedsandSettings::ApplyMeleeAppearance(category, "fire", "");
		Check(category.redsand.density == RedsandSettings::maxRedsandDotDensity && category.swordTrailMomentum == 2.f,
			"the densest legacy preset or the new speed limit was lost");
		migrated.diagnostics = true;
		RedsandSettings::Set(migrated);
		RedsandSettings::FlushPendingSave();
		const auto stored = Read(migration);
		RedsandSettings::Load(migration);
		Check(RedsandSettings::Get() == migrated && stored.find("\nSpacing=") == std::string::npos &&
			stored.find("\nRedsandSpacing=") == std::string::npos, "unified density did not round trip as a single setting");
		OldBlockStylesDoNotOverrideSparks(workspace);
        RandomSlashPersistsAndDefaultsOff(workspace);
		ExistingBowIniRemainsCompatible(workspace);
		SwordDefaultsAndSavedOverridesRemainDistinct(workspace);
		OldIniPreservesSavedOptionsAndIgnoresRemovedStaffSection(workspace);
		LanguageSelectionPersistsAndRejectsUnknownValues(workspace);
		IniInputCannotPublishInvalidMultipliers(workspace);
		DiagnosticLoggingDefaultsOffAndTogglesImmediately(workspace);
		ImmediatePublicationAndPersistenceRoundTrip(workspace);
		PrecisionTrailIsOptInAndPersists(workspace);
		ImpactLevelsAndSwitchesPreserveExistingSettings(workspace);
		ControlReleaseFlushesTheLastSliderValue(workspace);
		FailedSaveCanRecoverWithoutAnotherMenuChange(workspace);
		GameAndMenuThreadsObserveCompleteSnapshots(workspace);
		RGBAndCutLengthRoundTrip(workspace);
		const auto legacy = workspace.path / "retired-slash-settings.ini";
		Write(legacy, "[ImpactVFX]\nSlashDistance=3.25\nSlashSpread=0.35\nSlashDensity=1\nCutLength=1.7\nDensity=2.85\n[Color.SlashSparks]\nR=0.75\n[Color.Wave]\nR=0.15\n");
		RedsandSettings::Load(legacy);
		auto updated = RedsandSettings::Get();
		Check(updated.cutLength == 1.7f && updated.impactDensity == 2.85f &&
			updated.colors == RedsandSettings::defaultColors, "retired options altered retained settings");
		updated.cutWidth = 2.f;
		RedsandSettings::Set(updated);
		RedsandSettings::FlushPendingSave();
		const auto saved = Read(legacy);
		for (const auto* retired : { "SlashDistance", "SlashSpread", "SlashDensity", "Color.SlashSparks", "Color.Wave" }) {
			Check(saved.find(retired) == std::string::npos, "retired options were saved");
		}
		std::cout << "Runtime settings, persistence and thread snapshot checks passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
