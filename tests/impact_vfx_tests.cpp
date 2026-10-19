#include "../source/RedsandImpactRouting.h"
#include "../source/RedsandImpactManifest.h"

#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <fstream>
#include <chrono>

using namespace RedsandImpactVFX;

void Check(bool value, const char* message)
{
	if (!value) { throw std::runtime_error(message); }
}

Route Fixture(Kind kind = Kind::Impact, Category category = Category::Hit)
{
	Route route;
	route.plugin = "Skyrim.esm";
	route.form = 0x4BB52;
	route.kind = kind;
	route.category = category;
	route.originalModel = "Effects/Original.nif";
	route.baseModel = "StellarImpact/Hit/MetalBurst.nif";
	route.originalDuration = 0.1f;
	for (int i = 0; i < 64; ++i) { route.models[i] = "StellarImpact/Runtime/" + std::to_string(i) + ".nif"; }
	return route;
}

void Commit(Ownership& state, const Decision& decision)
{
	Check(!decision.conflict, "unexpected conflict");
	if (decision.write) { state.lastModel = decision.model; state.lastDuration = decision.duration; }
}

void Independent64Combinations()
{
	RedsandSettings::Config config;
	Check(ModelIndex(config) == 42, "default must be B2D2S2");
	Check(RedsandSettings::impactBrightnessLevels == std::array<float, 4>{ .10f, .50f, 1.0f, 12.f } &&
		RedsandSettings::impactDensityLevels == std::array<float, 4>{ .10f, .50f, 1.0f, 6.f } &&
		RedsandSettings::impactSizeLevels == std::array<float, 4>{ .10f, .50f, 1.0f, 6.f }, "UI levels no longer match expanded assets");
    config.impactBrightness = 1.37f; config.impactDensity = 2.85f; config.impactSize = .73f;
    Check(ModelIndex(config) == 42, "continuous values must use the unscaled base asset");
}

void UnavailableModelsOnlySkipAffectedRoutes()
{
	auto good = Fixture();
	auto broken = Fixture(); broken.models[12] = "StellarImpact/Missing.nif";
	auto sameBroken = broken; sameBroken.models[12] = "stellarimpact\\missing.nif";
	auto anotherGood = Fixture(); anotherGood.form = 0x123;
	ModelAvailability cache;
	std::unordered_map<std::string, unsigned> probes;
	auto probe = [&](const std::string& model) {
		const auto key = NormalizePath(model); ++probes[key];
		return key != "stellarimpact\\missing.nif";
	};
	unsigned usable = 0, failed = 0;
	for (const auto& route : { good, broken, sameBroken, anotherGood }) {
		if (const auto missing = FirstUnavailableModel(route, cache, probe)) {
			Check(NormalizePath(*missing) == "stellarimpact\\missing.nif", "missing model identity lost"); ++failed;
		} else { ++usable; }
	}
	Check(usable == 2 && failed == 2, "one missing resource disabled unrelated routes or admitted a failed cache entry");
	for (const auto& [_, count] : probes) { Check(count == 1, "resource cache repeated an asset open"); }
	broken = Fixture(); broken.originalModel = "StellarImpact/Missing.nif";
	Check(FirstUnavailableModel(broken, cache, probe).has_value(), "route admitted without its restoration model");
}

void NativeDisableRestoreAndReenable()
{
	auto route = Fixture();
	Ownership state{ route.baseModel, 0.85f };
	RedsandSettings::Config config;
	auto start = Plan(route, state, state.lastModel, state.lastDuration, config);
	Check(start.write && start.model == route.models[42] && start.duration == 0.85f, "initial selection failed");
	Commit(state, start);
	Check(!Plan(route, state, state.lastModel, state.lastDuration, config).write, "unchanged setting writes form again");
	config.impactHit = false;
	auto off = Plan(route, state, state.lastModel, state.lastDuration, config);
	Check(off.write && off.model == route.originalModel && off.duration == 0.1f, "disable did not restore original model and duration");
	Commit(state, off);
	config.impactHit = true; config.impactSize = 3;
	auto on = Plan(route, state, state.lastModel, state.lastDuration, config);
	Check(on.write && on.model == route.models[42] && on.duration == 0.85f, "reenable lost selected independent settings");
	Commit(state, on);
	config.impactEnabled = false;
	Check(Plan(route, state, state.lastModel, state.lastDuration, config).model == route.originalModel, "master switch does not restore native effect");
}

void ClassIsolationAndOwnSlashOff()
{
	RedsandSettings::Config config;
	config.impactHit = false;
	Check(!Enabled(Category::Hit, config) && Enabled(Category::Slash, config) && Enabled(Category::Block, config), "hit switch leaks categories");
	config.impactSlash = false; config.impactBlock = false;
	Check(!Enabled(Category::Slash, config) && !Enabled(Category::Block, config), "category switches ignored");
	auto route = Fixture(Kind::Impact, Category::Slash);
	route.originalModel.clear(); route.offModel = "StellarImpact/Runtime/Off.nif";
	Check(ValidRoute(route), "valid added-slash off model rejected");
	Ownership state{ route.models[42], 0.85f };
	const auto off = Plan(route, state, state.lastModel, state.lastDuration, config);
	Check(off.write && off.model == route.offModel && !off.model.empty(), "added slash disabled with unsafe empty model");
}

void ThirdPartyOwnershipProtection()
{
	auto route = Fixture();
	Ownership state{ route.models[42], 0.85f };
	RedsandSettings::Config config;
	Check(Plan(route, state, "ThirdParty/Other.nif", state.lastDuration, config).conflict, "third-party model overwritten");
	Check(Plan(route, state, state.lastModel, 9.0f, config).conflict, "third-party duration overwritten");
	state.conflicted = true;
	Check(Plan(route, state, state.lastModel, state.lastDuration, config).conflict, "relinquished record reclaimed");
	Check(KnownModel(route, "effects\\ORIGINAL.NIF") && KnownModel(route, route.baseModel) && KnownModel(route, route.models[63]), "safe initial source not recognized");
	Check(!KnownModel(route, "ThirdParty/Other.nif"), "unknown initial model accepted");
}

void ExplosionNeverOwnsData()
{
	auto route = Fixture(Kind::Explosion, Category::Block);
	Ownership state{ route.baseModel, 0.0f };
	RedsandSettings::Config config;
	const auto plan = Plan(route, state, state.lastModel, 123.0f, config);
	Check(plan.write && !plan.conflict, "explosion route incorrectly owns an IPCT duration field");
	config.impactBlock = false;
	Check(Plan(route, state, state.lastModel, 123.0f, config).model == route.originalModel, "explosion off does not restore original model");
}

void ProjectileSuppressionAndRestoration()
{
	auto route = Fixture(Kind::Projectile, Category::Block);
	route.baseModel = "StellarImpact/Runtime/Off.nif";
	route.models.fill(route.baseModel);
	route.originalDuration = std::numeric_limits<float>::quiet_NaN();
	route.duration = std::numeric_limits<float>::quiet_NaN();
	Check(ValidRoute(route), "projectile route incorrectly requires IPCT duration");
	Ownership state{ route.originalModel, 0.0f };
	RedsandSettings::Config config;
	auto on = Plan(route, state, state.lastModel, 123.0f, config);
	Check(on.write && !on.conflict && on.model == route.baseModel, "projectile blue layer not suppressed");
	Commit(state, on);
	config.impactSize = 3;
	Check(!Plan(route, state, state.lastModel, 456.0f, config).write, "constant off preset causes repeated writes");
	config.impactBlock = false;
	auto off = Plan(route, state, state.lastModel, 789.0f, config);
	Check(off.write && !off.conflict && off.model == route.originalModel, "projectile original model not restored");
	Commit(state, off);
	config.impactBlock = true; config.impactEnabled = false;
	Check(!Plan(route, state, state.lastModel, 999.0f, config).write, "master switch incorrectly suppresses original projectile");
}

void InvalidRoutesFailClosed()
{
	auto route = Fixture();
	Check(ValidRoute(route), "valid route rejected");
	for (const auto* path : { "", "../outside.nif", "C:/outside.nif", "Meshes/DoublePrefix.nif", "Data/DoublePrefix.nif", "/absolute.nif", "a//b.nif", "a/./b.nif", "a/../b.nif", "not-a-model.dds" }) {
		Check(!ValidModelPath(path), "unsafe model path accepted");
	}
	route.models[7] = "../bad.nif"; Check(!ValidRoute(route), "invalid single preset admitted");
	route = Fixture(); route.duration = std::numeric_limits<float>::quiet_NaN(); Check(!ValidRoute(route), "NaN duration admitted");
	route = Fixture(); route.originalDuration = -1.0f; Check(!ValidRoute(route), "negative original duration admitted");
	route = Fixture(); route.form = 0xFE000802; Check(!ValidRoute(route), "load-order FormID admitted");
	route = Fixture(); route.originalModel.clear(); Check(!ValidRoute(route), "missing original and off model admitted");
}

void ManifestParserRejectsBadAndDuplicateRoutes()
{
	const auto path = std::filesystem::temp_directory_path() / ("redsand-impact-manifest-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
	struct RemoveFixture { std::filesystem::path path; ~RemoveFixture() { std::error_code ignored; std::filesystem::remove(path, ignored); } } cleanup{ path };
	auto route = Fixture();
	std::string section = "\n[[Route]]\nplugin='Skyrim.esm'\nform=0x4BB52\ntype='IPCT'\ncategory='Hit'\n"
		"originalModel='Effects/Original.nif'\nbaseModel='StellarImpact/Hit/MetalBurst.nif'\noriginalDuration=0.1\nduration=0.85\nmodels=[";
	for (int i = 0; i < 64; ++i) { section += (i ? ", '" : "'") + route.models[i] + "'"; }
	section += "]\n";
	auto write = [&](const std::string& content) { std::ofstream output(path); output << content; Check(static_cast<bool>(output), "fixture write failed"); };
	write("schemaVersion=1\n" + section);
	const auto loaded = Manifest::ReadRoutes(path);
	Check(loaded.size() == 1 && loaded[0].models[42] == route.models[42] && loaded[0].originalDuration == 0.1f,
		"production manifest decoder lost valid fields");
	auto rejects = [&](const std::string& content) {
		write(content);
		bool rejected = false;
		try { (void)Manifest::ReadRoutes(path); } catch (const std::exception&) { rejected = true; }
		Check(rejected, "invalid manifest was accepted");
	};
	rejects("schemaVersion=1\n" + section + section);
	rejects("schemaVersion=2\n" + section);
	auto bad = section; bad.replace(bad.find("type='IPCT'"), 11, "type='WEAP'"); rejects("schemaVersion=1\n" + bad);
	bad = section; bad.replace(bad.find("originalDuration=0.1"), 20, "originalDuration=-1"); rejects("schemaVersion=1\n" + bad);
	rejects("schemaVersion=1\n[[Route]]\nmodels=[]\n");
	auto projectile = section;
	projectile.replace(projectile.find("type='IPCT'"), 11, "type='PROJ'");
	rejects("schemaVersion=1\n" + projectile);
	const auto durations = projectile.find("originalDuration=0.1\n");
	projectile.erase(durations, std::string("originalDuration=0.1\nduration=0.85\n").size());
	write("schemaVersion=1\n" + projectile);
	const auto projectileRoutes = Manifest::ReadRoutes(path);
	Check(projectileRoutes.size() == 1 && projectileRoutes[0].kind == Kind::Projectile, "PROJ parser failed");
	std::string legacy = "schemaVersion=1\n" + section;
	RedsandSettings::Config config;
	for (const auto* plugin : { "ValhallaCombat.esp", "CIF - HitSpark.esp", "Dplay_Block_SparksN.esp", "SCSI-HardCombatBase.esp",
		"SCSI-PerksOfSkyrim.esp", "SekiroCombat_II.esp", "SimpleTimedBlock.esp", "separated Slash Effects X.esp", "Other.esp" }) {
		legacy += "\n[[Route]]\nplugin='" + std::string(plugin) + "'\nmodels=[]\n";
		auto foreign = Fixture(); foreign.plugin = plugin;
		Check(!ValidRoute(foreign), "external combat record admitted for control");
		Ownership owned{ foreign.originalModel, foreign.originalDuration };
		const auto decision = Plan(foreign, owned, owned.lastModel, owned.lastDuration, config);
		Check(!decision.write && !decision.conflict, "external combat record was modified");
	}
	write(legacy);
	const auto filtered = Manifest::ReadRoutes(path);
	Check(filtered.size() == 1 && filtered[0].plugin == "Skyrim.esm" && filtered[0].models == route.models,
		"legacy combat redirects disabled native routes or survived filtering");
	write("schemaVersion=1\n[[Route]]\nplugin='ValhallaCombat.esp'\n");
	Check(Manifest::ReadRoutes(path).empty(), "legacy external-only manifest still took ownership of combat effects");
	auto mixedCase = Fixture(); mixedCase.plugin = "SKYRIM.ESM";
	Check(ValidRoute(mixedCase), "vanilla plugin comparison became case sensitive");
}

void NativeLayersCoverUnarmoredHitsAndPowerAttacks()
{
	RedsandSettings::Config config;
	auto layers=SelectNativeHitLayers(true,true,false,true,false,true,false,false,config);
	Check(layers.fallbackHit && layers.slash && !layers.refraction,"unarmored melee hit lost its fallback/slash");
	layers=SelectNativeHitLayers(true,true,false,true,true,true,true,true,config);
	Check(layers.fallbackHit && layers.slash && layers.refraction,"armored target lost native hit or single power-critical wave");
	layers=SelectNativeHitLayers(true,true,false,false,false,false,true,false,config);
	Check(layers.fallbackHit && !layers.slash && layers.refraction,"NPC blunt/power hit lost independent native layers");
	for(auto invalid: {SelectNativeHitLayers(false,true,false,true,false,true,true,true,config),
		SelectNativeHitLayers(true,false,false,true,false,true,true,true,config),
		SelectNativeHitLayers(true,true,true,true,false,true,true,true,config)}) {
		Check(!invalid.fallbackHit && !invalid.slash && !invalid.refraction,"blocked/bash/non-melee/non-actor hit produced visuals");
	}
	config.impactSlash=false;
	layers=SelectNativeHitLayers(true,true,false,true,false,true,true,false,config);
	Check(layers.fallbackHit && !layers.slash && layers.refraction,"slash toggle disabled independent hit layers");
	config.impactHit=false;config.impactSlash=true;
	layers=SelectNativeHitLayers(true,true,false,true,false,true,true,false,config);
	Check(!layers.fallbackHit && layers.slash && !layers.refraction,"hit toggle leaked native layers");
	config.impactEnabled=false;
	layers=SelectNativeHitLayers(true,true,false,true,false,true,true,true,config);
	Check(!layers.fallbackHit && !layers.slash && !layers.refraction,"master off leaked visuals");
}

void FormFreeNativePlaybackPolicies()
{
	RedsandSettings::Config config;
	NativeRoute block{ NativeLayer::Block, "StellarImpact/Runtime/B2D2S2/Block/NormalBlock.nif", .85f };
	Check(ValidNativeRoute(block), "valid form-free block route rejected");
	const auto models = BlockStyleModelPaths(block);
	const BlockStyleModels expected{
		"StellarImpact/Runtime/B2D2S2/Block/NormalBlock.nif", "StellarImpact/Runtime/B2D2S2/Block/TimedSparks.nif",
		"StellarImpact/Runtime/B2D2S2/Block/TimedFlare.nif", "StellarImpact/Runtime/B2D2S2/Block/PerfectRing.nif",
		"StellarImpact/Runtime/B2D2S2/Block/SimpleTimed.nif", "StellarImpact/Block/StarRing.nif"
	};
	Check(models == expected, "block style choices lost their installed native models");
	BlockStyleAvailability available{ true, true, true, true, true, true };
	for (unsigned index = 0; index < models.size(); ++index) {
		const auto style = static_cast<RedsandSettings::BlockStyle>(index);
		Check(BlockStyleIndex(style, available) == index, "available native block style was unreachable");
		Check(BlockStyleDuration(block.duration, BlockStyleIndex(style, available)) ==
			(style == RedsandSettings::BlockStyle::StarRing ? 2.f : block.duration), "block style lifespan clipped the ring or changed existing sparks");
		available[index] = false;
		Check(BlockStyleIndex(style, available) == 0, "missing block style did not fall back to normal sparks");
		Check(BlockStyleDuration(block.duration, BlockStyleIndex(style, available)) == block.duration,
			"missing ring fallback retained the longer ring lifespan");
		available[index] = true;
	}
	Check(BlockStyleIndex(RedsandSettings::BlockStyle::Count, available) == 0 &&
		BlockStyleIndex(static_cast<RedsandSettings::BlockStyle>(-1), available) == 0, "invalid block style escaped default fallback");
	Check(BlockStyleDuration(3.f, static_cast<unsigned>(RedsandSettings::BlockStyle::StarRing)) == 3.f,
		"ring lifespan shortened an explicitly longer native duration");
	auto unrelated = block; unrelated.layer = NativeLayer::Hit;
	Check(BlockStyleModelPaths(unrelated) == BlockStyleModels{}, "block style model selection leaked into hit effects");
	Check(SelectNativeBlock(true, true, false, config) && !SelectNativeBlock(true, true, true, config) &&
		!SelectNativeBlock(true, false, false, config), "ordinary blocks and bashes were conflated");
	Check(SelectNativeWorld(false, true, false, config) && !SelectNativeWorld(true, true, false, config) &&
		!SelectNativeWorld(false, false, false, config) && !SelectNativeWorld(false, true, true, config), "world pre-hit scope duplicated actors or admitted spells/bash");
	Check(!NativeWorldFallbackRequired(26,26) && NativeWorldFallbackRequired(26,25) && NativeWorldFallbackRequired(25,25) &&
		NativeWorldFallbackRequired(0,0), "world IPCT priority admitted duplicates or lost fallback");
	Check(SelectNativeProjectileLayer(true,false,true,config) == NativeLayer::Hit &&
		SelectNativeProjectileLayer(true,false,false,config) == NativeLayer::Stone &&
		SelectNativeProjectileLayer(true,true,true,config) == NativeLayer::Block &&
		!SelectNativeProjectileLayer(false,false,true,config), "arrow/native projectile policy lost actors, objects or block isolation");
	config.impactHit = false;
	Check(!SelectNativeProjectileLayer(true,false,true,config) && SelectNativeProjectileLayer(true,true,true,config) == NativeLayer::Block,
		"hit toggle disabled independent arrow blocks");
	config.impactBlock = false;
	Check(!SelectNativeBlock(true,true,false,config) &&
		!SelectNativeProjectileLayer(true,true,true,config), "block off failed to restore original routes");
	config.impactHit = true; config.impactBlock = true; config.impactEnabled = false;
	Check(!SelectNativeProjectileLayer(true,false,true,config) && !SelectNativeWorld(false,true,false,config) &&
		!SelectNativeBlock(true,true,false,config), "master off leaked form-free native visuals");
	block.duration = std::numeric_limits<float>::quiet_NaN(); Check(!ValidNativeRoute(block), "native NaN lifetime accepted");
	block.duration = .85f; block.model = "../outside.nif"; Check(!ValidNativeRoute(block), "unsafe native model accepted");
}

void NativeManifestRejectsFormDependenciesAndMalformedLayers()
{
	const auto path = std::filesystem::temp_directory_path() / ("redsand-native-manifest-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
	struct RemoveFixture { std::filesystem::path path; ~RemoveFixture() { std::error_code ignored; std::filesystem::remove(path, ignored); } } cleanup{ path };
	const std::string layer = "\n[[Native]]\nid='Hit'\nmodel='StellarImpact/Runtime/B2D2S2/Hit/MetalBurst.nif'\nduration=0.85\n";
	auto write = [&](const std::string& text) { std::ofstream file(path); file << text; Check(static_cast<bool>(file), "native fixture write failed"); };
	write("schemaVersion=1\n" + layer);
	const auto loaded = Manifest::ReadNativeRoutes(path);
	Check(loaded.size()==1 && loaded[0].layer==NativeLayer::Hit && loaded[0].duration==.85f && ValidNativeRoute(loaded[0]), "native model-only manifest lost fields");
	write("schemaVersion=1\nRoute=[]\n" + layer);
	Check(Manifest::ReadRoutes(path).empty() && Manifest::ReadNativeRoutes(path).size() == 1,
		"weapon-only installation required third-party impact redirects");
	write("schemaVersion=1\nRoute=[]\n");
	bool emptyRejected = false;
	try { (void)Manifest::ReadRoutes(path); } catch (const std::exception&) { emptyRejected = true; }
	Check(emptyRejected, "empty manifest without native effects was accepted");
	auto rejects = [&](const std::string& text) {
		write(text); bool rejected=false;
		try { (void)Manifest::ReadNativeRoutes(path); } catch(const std::exception&) { rejected=true; }
		Check(rejected,"malformed/form-dependent native layer was accepted");
	};
	rejects("schemaVersion=1\n" + layer + layer);
	rejects("schemaVersion=2\n" + layer);
	rejects("schemaVersion=1\nNative='not-an-array'\n");
	rejects("schemaVersion=1\n" + layer + "plugin='Anything.esp'\n");
	rejects("schemaVersion=1\n" + layer + "form=0x800\n");
	auto bad=layer; bad.replace(bad.find("id='Hit'"),8,"id='Unknown'"); rejects("schemaVersion=1\n"+bad);
	bad=layer; bad.replace(bad.find("duration=0.85"),13,"duration=nan"); rejects("schemaVersion=1\n"+bad);
	bad=layer; bad.erase(bad.find("duration=0.85")); rejects("schemaVersion=1\n"+bad);
	write("schemaVersion=1\n"); Check(Manifest::ReadNativeRoutes(path).empty(), "legacy route-only manifest became invalid");
}

int main(int argc, char** argv)
{
	try {
		Independent64Combinations(); NativeDisableRestoreAndReenable(); ClassIsolationAndOwnSlashOff();
		UnavailableModelsOnlySkipAffectedRoutes();
		NativeLayersCoverUnarmoredHitsAndPowerAttacks();
		FormFreeNativePlaybackPolicies();
		NativeManifestRejectsFormDependenciesAndMalformedLayers();
		ThirdPartyOwnershipProtection(); ExplosionNeverOwnsData(); ProjectileSuppressionAndRestoration(); InvalidRoutesFailClosed();
		ManifestParserRejectsBadAndDuplicateRoutes();
		if (argc == 2) {
			const auto loaded = Manifest::ReadRoutes(argv[1]);
			const auto native = Manifest::ReadNativeRoutes(argv[1]);
			Check(native.size()==5,"installed native manifest must cover all five independent layers");
			std::cout << "Actual manifest parsed by production C++ decoder: " << loaded.size() << " routes, " << native.size() << " form-free native layers\n";
		}
		std::cout << "Impact VFX: 64 presets, restoration, category isolation, ownership and validation passed\n";
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
