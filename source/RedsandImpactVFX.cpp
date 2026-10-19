#include "RedsandImpactVFX.h"
#include "RedsandImpactRouting.h"
#include "RedsandImpactManifest.h"
#include "RedsandLog.h"
#include "RedsandSlashFrame.h"
#include "RedsandAppearancePolicy.h"
#include "RedsandSlashRandom.h"
#include "RedsandBlockLightning.h"
#include "PrecisionHandler.h"
#include <RE/T/TESHitEvent.h>
#include <RE/S/ScriptEventSourceHolder.h>

#include <filesystem>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <unordered_set>
#include <RE/N/NiParticleSystem.h>
#include <RE/N/NiParticlesData.h>
#include <RE/N/NiFloatExtraData.h>

namespace RedsandImpactVFX
{
	namespace
	{
		struct LiveRoute
		{
			Route definition;
			RE::TESModel* model = nullptr;
			RE::BGSImpactData* impact = nullptr;
			Ownership ownership;
		};
		std::vector<LiveRoute> routes;
		std::mutex statusLock;
		Status status;
		std::atomic_bool initializationStarted{ false };
		std::atomic_bool ready{ false };
		bool applyPending = false;
		RedsandSettings::Config lastConfig;
		bool diagnosticsWereEnabled = false;
		std::array<std::optional<NativeRoute>, static_cast<unsigned>(NativeLayer::Count)> nativeRoutes;
		bool suppressBlockAdapter = false;
		std::size_t expectedVanillaRoutes = 0;
		struct RandomSlashRequest
		{
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::int32_t seed;
			std::chrono::steady_clock::time_point requested;
		};
		std::mutex randomSlashLock;
		std::vector<RandomSlashRequest> randomSlashRequests;
		std::atomic_bool hasRandomSlashRequests{ false };

		bool AttachSlashSeed(RE::NiAVObject* root, std::int32_t seed)
		{
			if (!root || root->name != RedsandAppearance::rootTag) { return false; }
			static const RE::BSFixedString name{ "RSRandomSlashSeed_v1" };
			if (auto* marker = netimmerse_cast<RE::NiFloatExtraData*>(root->GetExtraData(name))) {


				marker->value = static_cast<float>(seed);
				return true;
			}
			RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(name, static_cast<float>(seed)) };
			return extra && root->AddExtraData(extra.get());
		}

		void UpdateRandomSlashRequests()
		{
			if (!hasRandomSlashRequests.load(std::memory_order_acquire)) { return; }
			std::vector<RandomSlashRequest> retired;
			{
				std::lock_guard lock(randomSlashLock);
				std::erase_if(randomSlashRequests, [&](RandomSlashRequest& request) {
					if (AttachSlashSeed(request.effect->particleObject.get(), request.seed) ||
						std::chrono::steady_clock::now() - request.requested > std::chrono::seconds(2)) {
						retired.push_back(std::move(request)); return true;
					}
					return false;
				});
				hasRandomSlashRequests.store(!randomSlashRequests.empty(), std::memory_order_release);
			}

		}

		const NativeRoute* NativeRouteFor(NativeLayer layer)
		{
			const auto index = static_cast<unsigned>(layer);
			return index < nativeRoutes.size() && nativeRoutes[index] ? &*nativeRoutes[index] : nullptr;
		}

		std::string Identity(const Route& route)
		{
			return fmt::format("{}:{:X}", route.plugin, route.form);
		}

		void RecordConflict(LiveRoute& live, const std::string& currentModel)
		{
			live.ownership.conflicted = true;
			std::lock_guard lock(statusLock);
			++status.conflicts;
			--status.resolved;
			if (live.definition.form == 0x60C32 && NormalizePath(live.definition.plugin) == "valhallacombat.esp") {
				status.valhallaRingControlled = false;
			}
			status.detail = Identity(live.definition) + ": changed by another module: " + currentModel;
			logger::warn("Redsand Impact VFX relinquished {}", status.detail);
			RedsandLog::warn("Impact VFX relinquished {}", status.detail);
		}

		bool VanillaWorldRoutesAvailable(const RedsandSettings::Config& config)
		{
			std::size_t controlled = 0;
			for (auto& live : routes) {
				if (live.definition.kind != Kind::Impact || NormalizePath(live.definition.plugin) != "skyrim.esm" || live.ownership.conflicted) { continue; }
				const auto* current = live.model->GetModel();
				const auto decision = Plan(live.definition, live.ownership, current ? current : "", live.impact->data.effectDuration, config);
				if (decision.conflict) { RecordConflict(live, current ? current : ""); }
				else if (!decision.write) { ++controlled; }
			}
			return !NativeWorldFallbackRequired(expectedVanillaRoutes, controlled);
		}

		RedsandSlash::Vec3 FromNi(const RE::NiPoint3& value) { return { value.x, value.y, value.z }; }
		RedsandSlash::Vec3 FromHk(const RE::hkVector4& value)
		{
			return { value.quad.m128_f32[0], value.quad.m128_f32[1], value.quad.m128_f32[2] };
		}

		RedsandSlash::Vec3 CapsuleWorldAxis(const RE::hkpRigidBody* body)
		{
			if (!body) { return {}; }
			const auto* shape = body->collidable.GetShape();


			if (!shape || shape->type != RE::hkpShapeType::kCapsule) { return {}; }
			const auto* capsule = static_cast<const RE::hkpCapsuleShape*>(shape);
			const auto local = FromHk(capsule->vertexA) - FromHk(capsule->vertexB);
			const auto& rotation = body->motion.motionState.transform.rotation;
			return FromHk(rotation.col0) * local.x + FromHk(rotation.col1) * local.y + FromHk(rotation.col2) * local.z;
		}

		bool IsBlade(const RE::TESObjectWEAP* weapon)
		{
			if (!weapon) { return false; }
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kTwoHandSword: return true;
			case RE::WEAPON_TYPE::kTwoHandAxe: return !weapon->HasKeywordString("WeapTypeWarhammer");
			default: return false;
			}
		}

		struct PlaybackProbe
		{
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			const char* layer;
			std::chrono::steady_clock::time_point requested;
			unsigned samples = 0;
		};
		std::vector<PlaybackProbe> playbackProbes;
		std::array<unsigned, static_cast<unsigned>(NativeLayer::Count)> probeCounts{};

		void UpdatePlaybackProbes()
		{



			std::erase_if(playbackProbes, [](PlaybackProbe& probe) {
				auto* root = probe.effect->particleObject.get();
				const bool timeout = std::chrono::steady_clock::now() - probe.requested > std::chrono::seconds(2);
				if (!root) {
					if (timeout) { logger::warn("Redsand playback {}: async model unavailable after 2 seconds", probe.layer); }
					return timeout;
				}
				if (!timeout && probe.effect->age < (probe.samples == 0 ? .10f : .28f)) { return false; }
				std::vector<RE::NiAVObject*> pending{root};
				std::unordered_set<RE::NiAVObject*> visited;
				unsigned systems = 0, particles = 0, culled = 0;
				float alpha = 0, cutAlpha = -1.f;
				bool liveAppearance = false;
				while (!pending.empty() && visited.size() < 128) {
					auto* object = pending.back(); pending.pop_back();
					if (!object || !visited.emplace(object).second) { continue; }
					if (object->GetAppCulled()) { ++culled; }
					if (auto* geometry = netimmerse_cast<RE::BSGeometry*>(object)) {
						liveAppearance |= geometry->GetExtraData(RE::BSFixedString("RSRGBPeak_v1")) != nullptr;
						auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
						if (shader && shader->name == "RSRGBv1_4"sv && shader->GetMaterial()) { cutAlpha = shader->GetMaterial()->baseColor.alpha; }
					}
					if (auto* system = netimmerse_cast<RE::NiParticleSystem*>(object)) {
						++systems;
						if (auto data = system->GetParticlesRuntimeData().particleData) {
							const auto& live = data->GetParticlesRuntimeData();
							particles += data->GetActiveVertexCount();
							if (live.color) {
								for (unsigned i = 0; i < std::min<unsigned>({live.numVertices, live.maxNumVertices, 256}); ++i) {
									alpha = std::max(alpha, live.color[i].alpha);
								}
							}
						}
					}
					if (auto* node = object->AsNode()) {
						for (const auto& child : node->children) { if (child && pending.size() < 128) { pending.push_back(child.get()); } }
					}
				}
				auto* fade = root->AsFadeNode();
				logger::info("Redsand playback {} sample {}: age={} lifetime={} nodes={} systems={} activeParticles={} maxAlpha={} culledNodes={} rootFade={} rootScale={} rootName={} appearanceApplied={} cutAlpha={}",
					probe.layer, ++probe.samples, probe.effect->age, probe.effect->lifetime, visited.size(), systems,
					particles, alpha, culled, fade ? fade->currentFade : -1.f, root->world.scale, root->name.c_str(), liveAppearance, cutAlpha);
				return probe.samples == 2 || timeout;
			});
		}

		RE::BSTempEffectParticle* SpawnNativeLayer(NativeLayer selected,
			const RedsandSettings::Config& config, RE::TESObjectCELL* cell, const RE::NiMatrix3& basis,
			const RE::NiPoint3& position, bool slashContact = false)
		{
			const auto* route = NativeRouteFor(selected);
			if (!route || !cell || !Enabled(NativeCategory(selected), config)) { return nullptr; }
			const auto probeKind = static_cast<unsigned>(selected);
			const auto* layer = nativeLayerNames[probeKind].data();



			const auto seed = (selected == NativeLayer::Slash || (selected == NativeLayer::Hit && slashContact)) && RedsandSlashRandom::Enabled(config) ?
				(RedsandSlashRandom::attackSeed ? RedsandSlashRandom::attackSeed : RedsandSlashRandom::NextSeed()) : 0u;
			const auto sceneSeed = slashContact && selected == NativeLayer::Hit ? -static_cast<std::int32_t>(seed) : static_cast<std::int32_t>(seed);
			RedsandSlashRandom::AttackScope randomScope(seed ? seed : RedsandSlashRandom::attackSeed);
			auto* effect = RE::BSTempEffectParticle::Spawn(cell, route->duration,
				route->model.c_str(), basis, position, 1.f, 7, nullptr);
			if (effect && seed && !AttachSlashSeed(effect->particleObject.get(), sceneSeed)) {
				std::lock_guard lock(randomSlashLock);
				randomSlashRequests.push_back({ RE::NiPointer<RE::BSTempEffectParticle>(effect), sceneSeed, std::chrono::steady_clock::now() });
				hasRandomSlashRequests.store(true, std::memory_order_release);
			}
			if (config.diagnostics && probeCounts[probeKind] < 2) {
				++probeCounts[probeKind];
				logger::info("Redsand native {} request: factoryResult={} model={}", layer, effect != nullptr, route->model);
				if (effect) { playbackProbes.push_back({RE::NiPointer<RE::BSTempEffectParticle>(effect), layer, std::chrono::steady_clock::now()}); }
			}
			return effect;
		}

		RE::NiMatrix3 ToBasis(const RedsandSlash::Frame& frame)
		{
			RE::NiMatrix3 basis;
			basis.entry[0][0] = frame.x.x; basis.entry[1][0] = frame.x.y; basis.entry[2][0] = frame.x.z;
			basis.entry[0][1] = frame.y.x; basis.entry[1][1] = frame.y.y; basis.entry[2][1] = frame.y.z;
			basis.entry[0][2] = frame.z.x; basis.entry[1][2] = frame.z.y; basis.entry[2][2] = frame.z.z;
			return basis;
		}

		PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& precise)
		{


			if (!ready.load(std::memory_order_acquire) || !precise.attacker) { return {}; }
			const auto config = RedsandSettings::Get();
			const auto* entry = precise.attacker->GetAttackingWeapon();
			const auto* weapon = entry && entry->object ? entry->object->As<RE::TESObjectWEAP>() : nullptr;
			const auto* process = precise.attacker->GetActorRuntimeData().currentProcess;
			const bool bash = process && process->high && process->high->attackData &&
				process->high->attackData->data.flags.any(RE::AttackData::AttackFlag::kBashAttack);
			if (!SelectNativeWorld(precise.target && precise.target->As<RE::Actor>(), weapon && weapon->IsMelee(), bash, config)) { return {}; }



			if (VanillaWorldRoutesAvailable(config)) { return {}; }
			const auto frame = RedsandSlash::BuildFrame(FromNi(precise.hitPointVelocity),
				CapsuleWorldAxis(precise.hittingRigidBody), FromNi(precise.separatingNormal));
			auto* cell = precise.target ? precise.target->GetParentCell() : precise.attacker->GetParentCell();
			if (frame && cell && RedsandSlash::Finite(FromNi(precise.hitPos))) {
				if (SpawnNativeLayer(NativeLayer::Stone, config, cell, ToBasis(*frame), precise.hitPos)) {
					std::lock_guard lock(statusLock); ++status.nativeWorldHits;
				}
			}
			return {};
		}

		void OnPostHit(const PRECISION_API::PrecisionHitData& precise, const RE::HitData& hit)
		{
			if (!ready.load(std::memory_order_acquire) || !precise.attacker || !precise.target) { return; }
			const auto config = RedsandSettings::Get();
			auto* actor = precise.target->As<RE::Actor>();
			const bool blade = IsBlade(hit.weapon);

			const bool physicalMelee = !hit.weapon || hit.weapon->IsMelee();
			const bool blocked = hit.flags.any(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kBlockWithWeapon);
			const bool bash = hit.flags.any(RE::HitData::Flag::kBash, RE::HitData::Flag::kTimedBash);
            if (blocked && !bash) RedsandBlockLightning::Notify(precise.target, precise.attacker, precise.hitPos);
			const bool blockedOrBash = hit.flags.any(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kBlockWithWeapon,
				RE::HitData::Flag::kBash, RE::HitData::Flag::kTimedBash);
			const bool power = hit.flags.any(RE::HitData::Flag::kPowerAttack);
			const bool critical = hit.flags.any(RE::HitData::Flag::kCritical, RE::HitData::Flag::kCriticalOnDeath);
			const auto layers = SelectNativeHitLayers(actor != nullptr, physicalMelee, blockedOrBash, blade, false,
				precise.attacker->IsPlayerRef(), power, critical, config);
			const bool block = actor && SelectNativeBlock(physicalMelee, blocked, bash, config);
			static unsigned observed = 0;
			if (observed < 6) {
				logger::info("Redsand native hit {}: attacker={:X} target={:X} blockedOrBash={} power={} critical={} layers={}/{}/{}/{}",
					++observed, precise.attacker->GetFormID(), precise.target->GetFormID(), blockedOrBash,
					power, critical, layers.fallbackHit, layers.slash, layers.refraction, block);
			}
			if (!layers.fallbackHit && !layers.slash && !layers.refraction && !block) { return; }
			const auto frame = RedsandSlash::BuildFrame(FromNi(precise.hitPointVelocity),
				CapsuleWorldAxis(precise.hittingRigidBody), FromNi(precise.separatingNormal));
			auto* cell = precise.target->GetParentCell();
			if (!frame || !cell || !RedsandSlash::Finite(FromNi(precise.hitPos))) { return; }
			const auto basis = ToBasis(*frame);
			if (block && SpawnNativeLayer(NativeLayer::Block, config, cell, basis, precise.hitPos)) {
				std::lock_guard lock(statusLock); ++status.nativeBlocks;
			}
			const bool slashColor = layers.slash && NativeRouteFor(NativeLayer::Slash);
			const auto colorSeed = slashColor && RedsandSlashRandom::Enabled(config) ?
				(RedsandSlashRandom::attackSeed ? RedsandSlashRandom::attackSeed : RedsandSlashRandom::NextSeed()) : RedsandSlashRandom::attackSeed;
			RedsandSlashRandom::AttackScope contactColor(colorSeed);
			if (layers.fallbackHit) { SpawnNativeLayer(NativeLayer::Hit, config, cell, basis, precise.hitPos, slashColor); }
			if (layers.refraction) { SpawnNativeLayer(NativeLayer::Wave, config, cell, basis, precise.hitPos); }
			if (layers.slash) {
				auto* effect = SpawnNativeLayer(NativeLayer::Slash, config, cell, basis, precise.hitPos);
				std::lock_guard lock(statusLock);
				++status.slashHits;
				if (effect) { ++status.slashSpawned; } else { ++status.slashFailed; }
				if (frame->fallbackAxis) { ++status.slashFallbacks; }
			}
		}

		void OnArrowHit(RE::ObjectRefHandle targetHandle, RE::ObjectRefHandle causeHandle,
			RE::FormID targetID, RE::FormID causeID, RE::FormID source, RE::FormID projectile, bool blocked)
		{
			if (!ready.load(std::memory_order_acquire)) { return; }
			auto target = targetHandle.get();
			auto cause = causeHandle.get();
			auto* attacker = cause ? cause->As<RE::Actor>() : nullptr;
			if (!target || !attacker || target->GetFormID() != targetID || cause->GetFormID() != causeID || !target->Get3D()) { return; }
			if (auto* ui = RE::UI::GetSingleton(); !ui || ui->GameIsPaused()) { return; }
			const auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(source);
			const auto* projectileForm = RE::TESForm::LookupByID<RE::BGSProjectile>(projectile);
			const bool arrow = (weapon && (weapon->GetWeaponType() == RE::WEAPON_TYPE::kBow || weapon->GetWeaponType() == RE::WEAPON_TYPE::kCrossbow)) ||
				(projectileForm && projectileForm->IsArrow());
			auto* actor = target->As<RE::Actor>();
			const auto config = RedsandSettings::Get();
			const auto layer = SelectNativeProjectileLayer(arrow, blocked, actor != nullptr, config);
			if (!layer) { return; }


			auto position = target->Get3D()->worldBound.center;
			auto direction = position - attacker->GetPosition();
			const auto frame = RedsandSlash::BuildFrame(FromNi(direction), {}, {});
			if (!frame || !RedsandSlash::Finite(FromNi(position))) { return; }
			if (SpawnNativeLayer(*layer, config, target->GetParentCell(), ToBasis(*frame), position)) {
				std::lock_guard lock(statusLock); ++status.nativeArrowHits;
			}
		}

		class ArrowHitSink final : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (!ready.load(std::memory_order_acquire) || !event || !event->target || !event->cause ||
					event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) { return RE::BSEventNotifyControl::kContinue; }
				const auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(event->source);
				const auto* projectile = RE::TESForm::LookupByID<RE::BGSProjectile>(event->projectile);
				const bool arrow = (weapon && (weapon->GetWeaponType() == RE::WEAPON_TYPE::kBow || weapon->GetWeaponType() == RE::WEAPON_TYPE::kCrossbow)) ||
					(projectile && projectile->IsArrow());
				if (arrow) {


					const auto target = event->target->GetHandle(), cause = event->cause->GetHandle();
					const auto source = event->source, projectileID = event->projectile;
					const auto targetID = event->target->GetFormID(), causeID = event->cause->GetFormID();
					const bool blocked = event->flags.any(RE::TESHitEvent::Flag::kHitBlocked);
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([target, cause, targetID, causeID, source, projectileID, blocked] { OnArrowHit(target, cause, targetID, causeID, source, projectileID, blocked); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		ArrowHitSink arrowHitSink;

		bool AvailableModel(const std::string& model)
		{
			RE::BSResourceNiBinaryStream stream("Meshes\\" + model);
			return stream.good();
		}
	}

	Status GetStatus()
	{
		std::lock_guard lock(statusLock);
		return status;
	}

	const char* GetBlockImpactModel() noexcept
	{
		try {
			if (!ready.load(std::memory_order_acquire)) { return nullptr; }


			return NativeBlockBridgeModel(NativeRouteFor(NativeLayer::Block), true, suppressBlockAdapter, RedsandSettings::Get());
		} catch (...) { return nullptr; }
	}

	void Initialize()
	{


		if (initializationStarted.exchange(true)) { return; }
        RedsandBlockLightning::Initialize();
		RedsandAppearance::Install();
		Status next;
		const std::filesystem::path path = "Data/SKSE/Plugins/RedsandImpactVFX.toml";
		try {
			if (!std::filesystem::exists(path)) { next.availability = Availability::NotInstalled; }
			else {
				const auto definitions = Manifest::ReadRoutes(path);
				const auto nativeDefinitions = Manifest::ReadNativeRoutes(path);
				next.definitions = definitions.size() + nativeDefinitions.size();
				expectedVanillaRoutes = std::count_if(definitions.begin(), definitions.end(), [](const Route& definition) {
					return definition.kind == Kind::Impact && NormalizePath(definition.plugin) == "skyrim.esm";
				});
				for (const auto& definition : nativeDefinitions) {
					if (!AvailableModel(definition.model)) {
						++next.resourceFailures;
						next.detail = "Native layer missing asset: Meshes\\" + definition.model;
						logger::warn("Redsand Impact VFX native layer skipped: {}", next.detail);
						continue;
					}
					nativeRoutes[static_cast<unsigned>(definition.layer)] = definition;
					++next.resolved; ++next.nativeResolved;
				}
				auto* handler = RE::TESDataHandler::GetSingleton();
				Manifest::Require(handler != nullptr, "TESDataHandler unavailable");
				ModelAvailability checkedModels;
				for (const auto& definition : definitions) {
					auto* form = handler->LookupForm(definition.form, definition.plugin);
					if (!form) { ++next.missing; next.detail = "Missing optional form: " + Identity(definition); continue; }
					LiveRoute live;
					live.definition = definition;
					if (definition.kind == Kind::Impact) {
						live.impact = form->As<RE::BGSImpactData>();
						live.model = live.impact;
					} else if (definition.kind == Kind::Explosion) { live.model = form->As<RE::BGSExplosion>(); }
					else { live.model = form->As<RE::BGSProjectile>(); }
					if (!live.model) {
						++next.wrongTypes;
						next.detail = Identity(definition) + ": unexpected form type";
						logger::warn("Redsand Impact VFX route skipped: {}", next.detail);
						RedsandLog::warn("Impact VFX route skipped: {}", next.detail);
						continue;
					}
					const auto* initialModel = live.model ? live.model->GetModel() : nullptr;
					if (!initialModel || !KnownModel(definition, initialModel)) {
						++next.conflicts;
						++next.unknownModels;
						next.detail = Identity(definition) + ": unexpected initial model: " + (initialModel ? initialModel : "<none>");
						logger::warn("Redsand Impact VFX route skipped: {}", next.detail);
						RedsandLog::warn("Impact VFX route skipped: {}", next.detail);
						continue;
					}
					if (const auto missing = FirstUnavailableModel(definition, checkedModels, AvailableModel)) {
						++next.resourceFailures;
						next.detail = Identity(definition) + ": missing asset: Meshes\\" + *missing;
						logger::warn("Redsand Impact VFX route skipped: {}", next.detail);
						RedsandLog::warn("Impact VFX route skipped: {}", next.detail);
						continue;
					}
					live.ownership.lastModel = initialModel;
					live.ownership.lastDuration = live.impact ? live.impact->data.effectDuration : 0.0f;
					routes.push_back(std::move(live));
					++next.resolved;
					if (definition.form == 0x60C32 && NormalizePath(definition.plugin) == "valhallacombat.esp") {
						next.valhallaRingControlled = true;
					}
				}
				if (NativeRouteFor(NativeLayer::Slash) || NativeRouteFor(NativeLayer::Hit) ||
					NativeRouteFor(NativeLayer::Wave) || NativeRouteFor(NativeLayer::Block)) {
					next.slashCallback = PrecisionHandler::GetSingleton()->AddPostHitCallback(SKSE::GetPluginHandle(), OnPostHit);
					if (!next.slashCallback) { next.detail = "Native melee/block callback registration failed"; }
				}
				if (NativeRouteFor(NativeLayer::Stone)) {
					next.worldCallback = PrecisionHandler::GetSingleton()->AddPreHitCallback(SKSE::GetPluginHandle(), OnPreHit);
					if (!next.worldCallback) { next.detail = "Native world-hit callback registration failed"; }
				}
				if (NativeRouteFor(NativeLayer::Hit) || NativeRouteFor(NativeLayer::Stone) || NativeRouteFor(NativeLayer::Block)) {
					if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
						events->AddEventSink<RE::TESHitEvent>(&arrowHitSink);
						next.arrowCallback = true;
					}
				}
				suppressBlockAdapter = next.slashCallback && next.arrowCallback && NativeRouteFor(NativeLayer::Block) && AvailableModel("StellarImpact\\Runtime\\Off.nif");
				next.availability = routes.empty() && !next.nativeResolved ? Availability::InvalidRoutes : Availability::Ready;
				applyPending = true;
			}
		} catch (const std::exception& error) {


			routes.clear();
			for (auto& route : nativeRoutes) { route.reset(); }
			suppressBlockAdapter = false;
			next.resolved = 0;
			next.nativeResolved = 0;
			next.availability = Availability::InvalidRoutes;
			next.detail = error.what();
			RedsandLog::warn("Impact VFX initialization skipped: {}", error.what());
		}
		{
			std::lock_guard lock(statusLock);
			status = next;
		}
		const auto config = RedsandSettings::Get();
		logger::info("Redsand Impact VFX initialized: controls=2 state={} routes={}/{} missingForms={} wrongTypes={} missingAssets={} unknownModels={} nativeSlash={} blockAPI={} requestedPreset={} enabled={}/{}/{}/{} detail={}",
			static_cast<int>(next.availability), next.resolved, next.definitions, next.missing, next.wrongTypes,
			next.resourceFailures, next.unknownModels, next.slashCallback, NativeRouteFor(NativeLayer::Block) != nullptr, ModelIndex(config), config.impactEnabled,
			config.impactHit, config.impactSlash, config.impactBlock, next.detail);
		logger::info("Redsand Impact VFX native layers={} mainThreadWorld={} arrowEvents={} suppressDuplicateBlockAdapter={}",
			next.nativeResolved, next.worldCallback, next.arrowCallback, suppressBlockAdapter);


		ready.store(true, std::memory_order_release);
	}

	bool BindRandomSlashRoot(RE::NiNode* root)
	{
		if (!root || !hasRandomSlashRequests.load(std::memory_order_acquire)) { return false; }
		std::lock_guard lock(randomSlashLock);
		for (const auto& request : randomSlashRequests) {
			if (request.effect->particleObject.get() == root) { return AttachSlashSeed(root, request.seed); }
		}
		return false;
	}

	void Update()
	{
        RedsandBlockLightning::Update();
		if (!ready.load(std::memory_order_acquire)) { return; }
		UpdateRandomSlashRequests();
		UpdatePlaybackProbes();
		const auto config = RedsandSettings::Get();
		if (config.diagnostics && !diagnosticsWereEnabled) {
			const auto summary = GetStatus();
			RedsandLog::info("Impact VFX status: state={} definitions={} resolved={} missing={} conflicts={} nativeSlash={} applyPasses={} changed={} slashHits={} spawned={} failed={} fallback={} detail={}",
				static_cast<int>(summary.availability), summary.definitions, summary.resolved, summary.missing, summary.conflicts,
				summary.slashCallback, summary.applyPasses, summary.modelsChanged, summary.slashHits, summary.slashSpawned,
				summary.slashFailed, summary.slashFallbacks, summary.detail);
		}
		diagnosticsWereEnabled = config.diagnostics;
		if (!applyPending && config.impactEnabled == lastConfig.impactEnabled && config.impactHit == lastConfig.impactHit &&
			config.impactSlash == lastConfig.impactSlash && config.impactBlock == lastConfig.impactBlock &&
			config.preserveValhallaRing == lastConfig.preserveValhallaRing &&
			ModelIndex(config) == ModelIndex(lastConfig)) { return; }
		applyPending = false;
		lastConfig = config;
		std::size_t changed = 0;
		for (auto& live : routes) {
			if (live.ownership.conflicted) { continue; }
			const auto* currentModel = live.model->GetModel();
			const auto decision = Plan(live.definition, live.ownership, currentModel ? currentModel : "",
				live.impact ? live.impact->data.effectDuration : 0.0f, config);
			if (decision.conflict) {
				RecordConflict(live, currentModel ? currentModel : "");
				continue;
			}
			if (!decision.write) { continue; }
			live.model->SetModel(decision.model.c_str());
			if (live.impact) { live.impact->data.effectDuration = decision.duration; }
			const auto* appliedModel = live.model->GetModel();
			if (!appliedModel || NormalizePath(appliedModel) != NormalizePath(decision.model) ||
				(live.impact && live.impact->data.effectDuration != decision.duration)) {
				RecordConflict(live, appliedModel ? appliedModel : "<none>");
				continue;
			}
			live.ownership.lastModel = decision.model;
			live.ownership.lastDuration = decision.duration;
			++changed;
		}
		bool firstApplication = false;
		{
			std::lock_guard lock(statusLock);
			firstApplication = status.applyPasses == 0;
			++status.applyPasses;
			status.modelsChanged += changed;
			status.appliedIndex = ModelIndex(config);
			status.appliedEnabled = config.impactEnabled;
			status.appliedHit = config.impactHit;
			status.appliedSlash = config.impactSlash;
			status.appliedBlock = config.impactBlock;
		}
		if (firstApplication) {
			const auto summary = GetStatus();
			logger::info("Redsand Impact VFX first apply: preset={} enabled={}/{}/{}/{} changed={} controllable={} conflicts={} nativeSlash={}",
				ModelIndex(config), config.impactEnabled, config.impactHit, config.impactSlash, config.impactBlock,
				changed, summary.resolved, summary.conflicts, summary.slashCallback);
		}
		RedsandLog::info("Impact VFX applied: enabled={} hit={} slash={} block={} preset={} modelsChanged={}",
			config.impactEnabled, config.impactHit, config.impactSlash, config.impactBlock, ModelIndex(config), changed);
	}
}

extern "C" __declspec(dllexport) const char* __cdecl Redsand_GetBlockImpactModel_v1() noexcept
{
	return RedsandImpactVFX::GetBlockImpactModel();
}
