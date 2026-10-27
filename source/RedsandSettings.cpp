#include "RedsandSettings.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
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
			std::atomic<std::uint64_t> publishedRevision{ 0 };
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
			if (config.blockStyle >= BlockStyle::Count) { config.blockStyle = BlockStyle::NormalBlock; }
			config.swordRibbonLifetime = valid(config.swordRibbonLifetime, .8f, .03f, .2f);
			config.swordParticleLifetime = std::max(config.swordRibbonLifetime+.02f, valid(config.swordParticleLifetime, 1.2f, .05f, .3f));
			config.swordRibbonWidth = valid(config.swordRibbonWidth, 3.f, .15f, .5f);
			config.swordTrailMomentum = valid(config.swordTrailMomentum, 1.f, 0.f, .1f);
			config.swordRibbonFadeOut = valid(config.swordRibbonFadeOut, 1.2f, .06f, .25f);
			config.swordParticleFadeOut = std::max(config.swordRibbonLifetime+config.swordRibbonFadeOut+.04f-config.swordParticleLifetime, valid(config.swordParticleFadeOut, 1.6f, .08f, .4f));
			config.swordParticleGravity = valid(config.swordParticleGravity, 3.f, 0.f, .3f);
			config.swordBrightness = valid(config.swordBrightness, maxBrightness);
			config.swordDensity = valid(config.swordDensity, maxDensity);
			config.swordSpread = valid(config.swordSpread, maxSpread, 0.0f, defaultSpread);
			config.swordSize = valid(config.swordSize, maxSize, minSize);
			config.bowBrightness = valid(config.bowBrightness, maxBrightness);
			config.bowDensity = valid(config.bowDensity, maxDensity);
			config.impactBrightness = valid(config.impactBrightness, maxBrightness);
			config.impactDensity = valid(config.impactDensity, maxDensity);
			config.impactSize = valid(config.impactSize, maxSize, minSize);
			config.cutLength = valid(config.cutLength, maxCutLength, minCutLength);
			config.slashDistance = valid(config.slashDistance, maxSlashDistance, minSlashDistance, defaultSlashDistance);
			config.slashSpread = valid(config.slashSpread, maxSlashSpread, 0.f, 0.f);
			config.slashDensity = valid(config.slashDensity, maxSlashDensity, 0.f, 0.f);
			config.slashRandomRange = valid(config.slashRandomRange, 1.f, 0.f, .5f);
            config.blockLightningChance = valid(config.blockLightningChance, 1.f, 0.f, .1f);
            config.blockLightningBrightness = valid(config.blockLightningBrightness, maxBrightness);
            config.blockLightningSize = valid(config.blockLightningSize, 3.f, .1f);
            for (std::size_t k = 0; k < 3; ++k) {
                config.blockLightningColor[k] = valid(config.blockLightningColor[k], 1.f, 0.f, defaultLightningColor[k]);
            }
			for (std::size_t i = 0; i < config.colors.size(); ++i) {
				for (std::size_t k = 0; k < 3; ++k) { config.colors[i].rgb[k] = valid(config.colors[i].rgb[k], 1.f, 0.f, defaultColors[i].rgb[k]); }
			}
			if (config.enchantments.values) {
				auto profiles = std::make_shared<EnchantmentProfiles::Map>(*config.enchantments.values);
				for (auto& [key, appearance] : *profiles) {
					appearance.swordRibbonLifetime = valid(appearance.swordRibbonLifetime, .8f, .03f, .2f);
					appearance.swordParticleLifetime = std::max(appearance.swordRibbonLifetime+.02f, valid(appearance.swordParticleLifetime, 1.2f, .05f, .3f));
					appearance.swordRibbonWidth = valid(appearance.swordRibbonWidth, 3.f, .15f, .5f);
					appearance.swordTrailMomentum = valid(appearance.swordTrailMomentum, 1.f, 0.f, .1f);
					appearance.swordRibbonFadeOut = valid(appearance.swordRibbonFadeOut, 1.2f, .06f, .25f);
					appearance.swordParticleFadeOut = std::max(appearance.swordRibbonLifetime+appearance.swordRibbonFadeOut+.04f-appearance.swordParticleLifetime, valid(appearance.swordParticleFadeOut, 1.6f, .08f, .4f));
					appearance.swordParticleGravity = valid(appearance.swordParticleGravity, 3.f, 0.f, .3f);
					appearance.brightness = valid(appearance.brightness, maxBrightness);
					appearance.density = valid(appearance.density, maxDensity);
					appearance.size = valid(appearance.size, maxSize, minSize);
					appearance.spread = valid(appearance.spread, maxSpread);
					appearance.bowBrightness = valid(appearance.bowBrightness, maxBrightness);
					appearance.bowDensity = valid(appearance.bowDensity, maxDensity);
					appearance.bowSize = valid(appearance.bowSize, maxSize, minSize);
					for (auto& channel : appearance.bowColor.rgb) { channel = valid(channel, 1.f, 0.f, 0.f); }
					for (auto& channel : appearance.color.rgb) { channel = valid(channel, 1.f, 0.f, 0.f); }
				}
				std::erase_if(*profiles, [](const auto& item) { return item.first.empty() || item.first.find_first_of("[];#\r\n=") != std::string::npos; });
				config.enchantments.values = std::move(profiles);
			}
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

		void ReadLevel(std::string_view value, float& output, const std::array<float, 4>& levels)
		{
			int result = 2;
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
			if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) { output = levels[std::clamp(result, 0, 3)]; }
		}

		void ReadBlockStyle(std::string_view value, BlockStyle& output)
		{
			if (value.empty()) { return; }
			unsigned result = 0;
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
			if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result < static_cast<unsigned>(BlockStyle::Count)) {
				output = static_cast<BlockStyle>(result);
			}
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
					else if (key == "releasedtrail") { ReadBool(value, result.swordReleasedTrail); }
					else if (key == "ribbonlifetime") { ReadFloat(value, result.swordRibbonLifetime); }
					else if (key == "particlelifetime") { ReadFloat(value, result.swordParticleLifetime); }
					else if (key == "ribbonwidth") { ReadFloat(value, result.swordRibbonWidth); }
					else if (key == "trailmomentum") { ReadFloat(value, result.swordTrailMomentum); }
					else if (key == "ribbonfadeout") { ReadFloat(value, result.swordRibbonFadeOut); }
					else if (key == "particlefadeout") { ReadFloat(value, result.swordParticleFadeOut); }
					else if (key == "particlegravity") { ReadFloat(value, result.swordParticleGravity); }
					else if (key == "keepprecisiontrail") { ReadBool(value, result.swordKeepPrecisionTrail); }
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
				} else if (section == "impactvfx") {
					if (key == "enabled") { ReadBool(value, result.impactEnabled); }
					else if (key == "hit") { ReadBool(value, result.impactHit); }
					else if (key == "slash") { ReadBool(value, result.impactSlash); }
					else if (key == "randomslashcolor") { ReadBool(value, result.slashRandomColor); }
					else if (key == "randomslashvalues") { ReadBool(value, result.slashRandomValues); }
					else if (key == "randomslashrange") { ReadFloat(value, result.slashRandomRange); }
					else if (key == "block") { ReadBool(value, result.impactBlock); }
					else if (key == "blockstyle") { ReadBlockStyle(value, result.blockStyle); }
					else if (key == "brightnesslevel") { ReadLevel(value, result.impactBrightness, impactBrightnessLevels); }
					else if (key == "densitylevel") { ReadLevel(value, result.impactDensity, impactDensityLevels); }
					else if (key == "sizelevel") { ReadLevel(value, result.impactSize, impactSizeLevels); }
					else if (key == "brightness") { ReadFloat(value, result.impactBrightness); }
					else if (key == "density") { ReadFloat(value, result.impactDensity); }
					else if (key == "size") { ReadFloat(value, result.impactSize); }
					else if (key == "cutlength") { ReadFloat(value, result.cutLength); }
					else if (key == "slashdistance") { ReadFloat(value, result.slashDistance); }
					else if (key == "slashspread") { ReadFloat(value, result.slashSpread); }
					else if (key == "slashdensity") { ReadFloat(value, result.slashDensity); }
                } else if (section == "blocklightning") {
                    if (key == "enabled") { ReadBool(value, result.blockLightning); }
                    else if (key == "randomtrigger") { ReadBool(value, result.blockLightningRandomTrigger); }
                    else if (key == "randomcolor") { ReadBool(value, result.blockLightningRandomColor); }
                    else if (key == "chance") { ReadFloat(value, result.blockLightningChance); }
                    else if (key == "brightness") { ReadFloat(value, result.blockLightningBrightness); }
                    else if (key == "size") { ReadFloat(value, result.blockLightningSize); }
                    else if (key == "r") { ReadFloat(value, result.blockLightningColor[0]); }
                    else if (key == "g") { ReadFloat(value, result.blockLightningColor[1]); }
                    else if (key == "b") { ReadFloat(value, result.blockLightningColor[2]); }
				} else if (section.starts_with("enchantment.")) {
					const auto profileKey = section.substr(12);
					auto appearance = result.enchantments.Find(profileKey) ? *result.enchantments.Find(profileKey) : EnchantmentAppearance{};
					if (key == "enabled") { ReadBool(value, appearance.enabled); }
					else if (key == "customcolor") { ReadBool(value, appearance.color.custom); }
					else if (key == "r") { ReadFloat(value, appearance.color.rgb[0]); }
					else if (key == "g") { ReadFloat(value, appearance.color.rgb[1]); }
					else if (key == "b") { ReadFloat(value, appearance.color.rgb[2]); }
					else if (key == "brightness") { ReadFloat(value, appearance.brightness); }
					else if (key == "density") { ReadFloat(value, appearance.density); }
					else if (key == "size") { ReadFloat(value, appearance.size); }
					else if (key == "spread") { ReadFloat(value, appearance.spread); }
					else if (key == "releasedtrail") { ReadBool(value, appearance.swordReleasedTrail); }
					else if (key == "ribbonlifetime") { ReadFloat(value, appearance.swordRibbonLifetime); }
					else if (key == "particlelifetime") { ReadFloat(value, appearance.swordParticleLifetime); }
					else if (key == "ribbonwidth") { ReadFloat(value, appearance.swordRibbonWidth); }
					else if (key == "trailmomentum") { ReadFloat(value, appearance.swordTrailMomentum); }
					else if (key == "ribbonfadeout") { ReadFloat(value, appearance.swordRibbonFadeOut); }
					else if (key == "particlefadeout") { ReadFloat(value, appearance.swordParticleFadeOut); }
					else if (key == "particlegravity") { ReadFloat(value, appearance.swordParticleGravity); }
					else if (key == "swordenabled") { ReadBool(value, appearance.swordEnabled); }
					else if (key == "swordplayer") { ReadBool(value, appearance.swordPlayer); }
					else if (key == "swordnpc") { ReadBool(value, appearance.swordNPC); }
					else if (key == "swordtip") { ReadBool(value, appearance.swordTip); }
					else if (key == "swordkeepprecisiontrail") { ReadBool(value, appearance.swordKeepPrecisionTrail); }
					else if (key == "bowenabled") { ReadBool(value, appearance.bowEnabled); }
					else if (key == "bowplayer") { ReadBool(value, appearance.bowPlayer); }
					else if (key == "bownpc") { ReadBool(value, appearance.bowNPC); }
					else if (key == "bowgathering") { ReadBool(value, appearance.bowGathering); }
					else if (key == "bowrings") { ReadBool(value, appearance.bowRings); }
					else if (key == "bowtrails") { ReadBool(value, appearance.bowTrails); }
					else if (key == "bowcustomcolor") { ReadBool(value, appearance.bowColor.custom); }
					else if (key == "bowbrightness") { ReadFloat(value, appearance.bowBrightness); }
					else if (key == "bowdensity") { ReadFloat(value, appearance.bowDensity); }
					else if (key == "bowsize") { ReadFloat(value, appearance.bowSize); }
					else if (key == "bowr") { ReadFloat(value, appearance.bowColor.rgb[0]); }
					else if (key == "bowg") { ReadFloat(value, appearance.bowColor.rgb[1]); }
					else if (key == "bowb") { ReadFloat(value, appearance.bowColor.rgb[2]); }
					result.enchantments.Set(profileKey, appearance);
				} else if (section.starts_with("color.")) {
					for (std::size_t i = 0; i < colorNames.size(); ++i) {
						if (section.substr(6) != Lower(colorNames[i])) { continue; }
						auto& color = result.colors[i];
						if (key == "enabled") { ReadBool(value, color.custom); }
						else if (key == "r") { ReadFloat(value, color.rgb[0]); }
						else if (key == "g") { ReadFloat(value, color.rgb[1]); }
						else if (key == "b") { ReadFloat(value, color.rgb[2]); }
					}
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
					<< "; Brightness: 0..20. Density: 0..10. 1 is the default multiplier.\n"
					<< "; Melee spread: 0..10. Size: 0.05..10. Cut length: 0.05..20.\n"
					<< "; Slash count: 0..1. Unit count retains the previous absolute particle limit.\n"
					<< "[Menu]\nLanguage=" << (config.language == Language::Chinese ? "Chinese" : "English") << '\n'
					<< "DiagnosticLogging=" << config.diagnostics << "\n\n"
					<< "[SwordParticles]\n"
					<< "Enabled=" << config.swordEnabled << '\n'
					<< "Player=" << config.swordPlayer << '\n'
					<< "NPC=" << config.swordNPC << '\n'
					<< "Tip=" << config.swordTip << '\n'
					<< "KeepPrecisionTrail=" << config.swordKeepPrecisionTrail << '\n'
					<< "ReleasedTrail=" << config.swordReleasedTrail << '\n'
					<< "RibbonLifetime=" << config.swordRibbonLifetime << '\n'
					<< "ParticleLifetime=" << config.swordParticleLifetime << '\n'
					<< "RibbonWidth=" << config.swordRibbonWidth << '\n'
					<< "TrailMomentum=" << config.swordTrailMomentum << '\n'
					<< "RibbonFadeOut=" << config.swordRibbonFadeOut << '\n'
					<< "ParticleFadeOut=" << config.swordParticleFadeOut << '\n'
					<< "ParticleGravity=" << config.swordParticleGravity << '\n'
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
					<< "Trails=" << config.bowTrails << "\n\n"
					<< "; Impact controls are continuous multipliers; density controls newly emitted particles.\n"
					<< "; Disabled categories restore their original visuals.\n"
					<< "[ImpactVFX]\nEnabled=" << config.impactEnabled << '\n'
					<< "Hit=" << config.impactHit << '\n'
					<< "Slash=" << config.impactSlash << '\n'
					<< "RandomSlashColor=" << config.slashRandomColor << '\n'
					<< "RandomSlashValues=" << config.slashRandomValues << '\n'
					<< "RandomSlashRange=" << config.slashRandomRange << '\n'
					<< "Block=" << config.impactBlock << '\n'
					<< "BlockStyle=" << static_cast<unsigned>(config.blockStyle) << '\n'
					<< "Brightness=" << config.impactBrightness << '\n'
					<< "Density=" << config.impactDensity << '\n'
					<< "Size=" << config.impactSize << '\n'
					<< "CutLength=" << config.cutLength << '\n'
					<< "SlashDistance=" << config.slashDistance << '\n'
					<< "SlashSpread=" << config.slashSpread << '\n'
					<< "SlashDensity=" << config.slashDensity << '\n';
                output << "\n; Player successful blocks only. Adds visuals; never changes time or block behavior.\n"
                    << "[BlockLightning]\nEnabled=" << config.blockLightning << '\n'
                    << "RandomTrigger=" << config.blockLightningRandomTrigger << '\n'
                    << "RandomColor=" << config.blockLightningRandomColor << '\n'
                    << "Chance=" << config.blockLightningChance << '\n'
                    << "Brightness=" << config.blockLightningBrightness << '\n'
                    << "Size=" << config.blockLightningSize << '\n'
                    << "R=" << config.blockLightningColor[0] << '\n'
                    << "G=" << config.blockLightningColor[1] << '\n'
                    << "B=" << config.blockLightningColor[2] << '\n';
				output << "; RGB storage is normalized 0..1; the color picker displays RGB values.\n";
				for (std::size_t i = 0; i < colorNames.size(); ++i) {
					const auto& color = config.colors[i];
					output << "\n[Color." << colorNames[i] << "]\nEnabled=" << color.custom
						<< "\nR=" << color.rgb[0] << "\nG=" << color.rgb[1] << "\nB=" << color.rgb[2] << '\n';
				}
				if (config.enchantments.values) {
					for (const auto& [key, appearance] : *config.enchantments.values) {
						output << "\n[Enchantment." << key << "]\nEnabled=" << appearance.enabled
							<< "\nCustomColor=" << appearance.color.custom
							<< "\nR=" << appearance.color.rgb[0] << "\nG=" << appearance.color.rgb[1] << "\nB=" << appearance.color.rgb[2]
							<< "\nBrightness=" << appearance.brightness << "\nDensity=" << appearance.density
							<< "\nSize=" << appearance.size << "\nSpread=" << appearance.spread
							<< "\nReleasedTrail=" << appearance.swordReleasedTrail
							<< "\nRibbonLifetime=" << appearance.swordRibbonLifetime
							<< "\nParticleLifetime=" << appearance.swordParticleLifetime
							<< "\nRibbonWidth=" << appearance.swordRibbonWidth
							<< "\nTrailMomentum=" << appearance.swordTrailMomentum
							<< "\nRibbonFadeOut=" << appearance.swordRibbonFadeOut
							<< "\nParticleFadeOut=" << appearance.swordParticleFadeOut
							<< "\nParticleGravity=" << appearance.swordParticleGravity
							<< "\nSwordEnabled=" << appearance.swordEnabled
							<< "\nSwordPlayer=" << appearance.swordPlayer
							<< "\nSwordNPC=" << appearance.swordNPC
							<< "\nSwordTip=" << appearance.swordTip
							<< "\nSwordKeepPrecisionTrail=" << appearance.swordKeepPrecisionTrail
							<< "\nBowEnabled=" << appearance.bowEnabled
							<< "\nBowPlayer=" << appearance.bowPlayer
							<< "\nBowNPC=" << appearance.bowNPC
							<< "\nBowGathering=" << appearance.bowGathering
							<< "\nBowRings=" << appearance.bowRings
							<< "\nBowTrails=" << appearance.bowTrails
							<< "\nBowCustomColor=" << appearance.bowColor.custom
							<< "\nBowBrightness=" << appearance.bowBrightness
							<< "\nBowDensity=" << appearance.bowDensity
							<< "\nBowSize=" << appearance.bowSize
							<< "\nBowR=" << appearance.bowColor.rgb[0]
							<< "\nBowG=" << appearance.bowColor.rgb[1]
							<< "\nBowB=" << appearance.bowColor.rgb[2] << '\n';
					}
				}
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
		struct Snapshot { std::uint64_t revision = std::numeric_limits<std::uint64_t>::max(); Config config; };
		thread_local Snapshot cached;



		if (cached.revision != state.publishedRevision.load(std::memory_order_acquire)) {
			std::lock_guard lock(state.valuesLock);
			cached.config = state.config;
			cached.revision = state.revision;
		}
		return cached.config;
	}

	void Set(Config config)
	{
		config = Normalize(config);
		auto& state = GetState();
		std::lock_guard lock(state.valuesLock);
		if (config == state.config) { return; }
		state.config = config;
		++state.revision;
		state.publishedRevision.store(state.revision, std::memory_order_release);
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
		state.publishedRevision.store(state.revision, std::memory_order_release);
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
