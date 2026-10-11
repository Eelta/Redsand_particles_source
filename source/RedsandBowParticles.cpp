#include "RedsandBowParticles.h"
#include "RedsandLog.h"
#include "RedsandMotionTrace.h"

#include "AttackTrail.h"
#include "RedsandBowSimulation.h"
#include "RedsandBowEventState.h"
#include "RedsandBowStreamBudget.h"
#include "RedsandSettings.h"
#include "RedsandWeaponProfiles.h"
#include "Settings.h"
#include "RE/N/NiParticleSystem.h"
#include "RE/N/NiPSysData.h"
#include "RE/N/NiPSysModifier.h"
#include "RE/N/NiSourceTexture.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace RedsandBowParticles
{
	namespace
	{
		using Redsand::Bow::Vec3;
		using Redsand::Bow::Simulation;
		using Redsand::Bow::ChargeSignal;
		using Redsand::Bow::OrderedChargeState;
		static_assert(static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowDraw) == 8);
		static_assert(static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowDrawn) == 10);
		static_assert(static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowReleased) == 12);
		constexpr std::size_t maxStreams = 32;
		constexpr std::size_t maxQueuedSamples = 2048;
		constexpr char particleMesh[] = "Effects/WeaponTrails/ElementalDesert/DefaultYellow.nif";
		std::atomic_bool enabled = false;
		std::atomic_uint64_t generation = 0;
		std::atomic<RE::FormID> rapidComboWeapon = 0;
		std::mutex sampleLock;
		std::atomic_uint64_t eventSequence = 0;
		std::atomic_bool diagnosticCapture = false;
		Redsand::MotionTrace motionTrace;
		struct CaptureDiagnostics
		{
			std::atomic_uint64_t calls = 0, captured = 0, missingWeapon = 0, unsupportedWeapon = 0, reservationRejected = 0;
			std::atomic_uint64_t chargeEvents = 0, chargeDropped = 0, sourceRejected = 0;
		} captureDiagnostics;
		struct ChargeDiagnostics
		{
			std::uint32_t native = 0;
			int attacking = -1, drawn = -1;
			bool observed = false, nocked = false, active = false;
		} chargeDiagnostics;
		std::uint64_t diagnosticSamples = 0, diagnosticBirths = 0;
		std::chrono::steady_clock::time_point nextDiagnostic;
		std::unordered_map<RE::ActorHandle, OrderedChargeState> chargeStates;
		std::unordered_map<RE::ProjectileHandle, Redsand::Bow::ShotState> shots;
		Redsand::Bow::ChargeView playerView;
		std::atomic_uintptr_t playerEventSource = 0;
		std::atomic_uint64_t playerViewEpoch = 0;
		std::vector<RE::BShkbAnimationGraphPtr> playerGraphs;
		struct ChargeEvent { RE::ActorHandle actor; ChargeSignal signal; std::uintptr_t source; std::uint64_t viewEpoch; std::uint64_t sequence; };
		std::vector<ChargeEvent> pendingChargeEvents;
		void QueueCharge(RE::Actor* actor, ChargeSignal signal, std::uintptr_t source = 0, std::uint64_t viewEpoch = 0)
		{
			if (!actor || signal == ChargeSignal::None || !enabled.load()) { return; }
			const auto eventGeneration = generation.load();
			const auto sequence = eventSequence.fetch_add(1) + 1;
			const auto handle = actor->GetHandle();
			std::lock_guard lock(sampleLock);
			if (enabled.load() && eventGeneration == generation.load() && pendingChargeEvents.size() < 1024) {
				pendingChargeEvents.push_back({ handle, signal, source, viewEpoch, sequence });
				if (diagnosticCapture.load(std::memory_order_relaxed)) { captureDiagnostics.chargeEvents.fetch_add(1, std::memory_order_relaxed); }
			} else if (diagnosticCapture.load(std::memory_order_relaxed)) {
				captureDiagnostics.chargeDropped.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void ProcessAnimationEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
		{
			if (!event || !event->holder || !enabled.load()) { return; }
			const auto* tag = event->tag.c_str();
			const auto* payload = event->payload.c_str();
			if (!tag) { return; }
			const auto signal = Redsand::Bow::ClassifyChargeEvent(tag, payload ? payload : "");
			if (signal == ChargeSignal::None) { return; }
			auto* actor = event->holder->As<RE::Actor>();
			if (!actor) { return; }
			if (actor->IsPlayerRef()) {
				const auto epoch = playerViewEpoch.load();
				const auto identity = reinterpret_cast<std::uintptr_t>(source);
				if (!identity || identity != playerEventSource.load()) {
					if (diagnosticCapture.load(std::memory_order_relaxed)) { captureDiagnostics.sourceRejected.fetch_add(1, std::memory_order_relaxed); }
					return;
				}
				QueueCharge(actor, signal, identity, epoch);
				return;
			}
			RE::BSAnimationGraphManagerPtr manager;
			actor->GetAnimationGraphManager(manager);
			if (manager) {
				const auto active = manager->GetRuntimeData().activeGraph;
				if (active < manager->graphs.size() && manager->graphs[active] && manager->graphs[active]->GetEventSource<RE::BSAnimationGraphEvent>() != source) { return; }
			}
			QueueCharge(actor, signal);
		}

		class PlayerAnimationSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override
			{
				if (event && event->holder && event->holder->IsPlayerRef()) {
					if (diagnosticCapture.load(std::memory_order_relaxed)) {
						motionTrace.Capture(enabled.load(), reinterpret_cast<std::uintptr_t>(source), playerEventSource.load(),
							playerViewEpoch.load(), event->tag.c_str(), event->payload.c_str(), [] {
								return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::steady_clock::now().time_since_epoch()).count());
							});
					}
					ProcessAnimationEvent(event, source);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		PlayerAnimationSink playerAnimationSink;

		class MotionInputSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event, RE::BSTEventSource<SKSE::ModCallbackEvent>* source) override
			{
				if (event && diagnosticCapture.load(std::memory_order_relaxed) && event->eventName.c_str()) {
					const std::string_view name = event->eventName.c_str();
					if (name == "InputManager_ActionTriggered" || name == "InputManager_ActionReleased" || name == "InputManager_MotionTriggered") {
						const auto identity = reinterpret_cast<std::uintptr_t>(source);
						motionTrace.Capture(enabled.load(), identity, identity, playerViewEpoch.load(), event->eventName.c_str(), event->strArg.c_str(), [] {
							return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now().time_since_epoch()).count());
						}, event->numArg, true);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class ActionSink : public RE::BSTEventSink<SKSE::ActionEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* event, RE::BSTEventSource<SKSE::ActionEvent>*) override
			{
				if (event) {
					if (event->type == SKSE::ActionEvent::Type::kBowDraw) { QueueCharge(event->actor, ChargeSignal::Start); }
					else if (event->type == SKSE::ActionEvent::Type::kBowRelease || event->type == SKSE::ActionEvent::Type::kBeginSheathe) {
						QueueCharge(event->actor, ChargeSignal::Stop);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		Vec3 ToVec(const RE::NiPoint3& p) { return { p.x, p.y, p.z }; }
		RE::NiPoint3 ToNi(Vec3 p) { return { p.x, p.y, p.z }; }

		struct ArrowSample
		{
			std::uint64_t sequence = 0;
			std::uint64_t lastSequence = 0;
			RE::ProjectileHandle handle;
			RE::ObjectRefHandle shooter;
			RE::FormID cellID = 0;
			RE::FormID weaponID = 0;
			Vec3 from, to, direction;
			float delta = 0.f, livingTime = 0.f, power = 1.f;
			float latestLivingTime = -1.f;
			bool flying = false, continuous = false;
			Redsand::Bow::FlightPath path;
		};
		Redsand::Bow::SampleQueue<ArrowSample> pendingSamples{ maxQueuedSamples };
		Redsand::Bow::BirthReservations<RE::ProjectileHandle> birthReservations{ maxQueuedSamples };
		std::unordered_set<RE::ProjectileHandle> batchBirths;

		struct Stream
		{
			Simulation simulation;
			RE::ActorHandle actor;
			RE::ProjectileHandle projectile;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::NiPointer<RE::NiParticleSystem> system;
			RE::NiAVObject* rendererRoot = nullptr;
			RE::NiColorA color{ 0.025f, 0.080f, 0.650f, 1.f };
			std::string meshPath;
			RE::FormID weaponID = 0;
			Vec3 tip;
			float age = 0.f, chargeTime = 0.f, silence = 0.f;
			float rendererStartAge = 0.f, rendererBrightness = -1.f;
			std::uint64_t chargeSequence = 0;
			bool charging = false, emitting = true, seen = false;
			bool rendererWasAttached = false;
			bool rendererResourcesPending = false;
			std::chrono::steady_clock::time_point nextRendererDiagnostic;
			std::uint16_t rendererSubmittedCount = 0;
			Vec3 rendererSubmittedFirst;

			explicit Stream(std::uint32_t seed) : simulation(seed) {}
			void Stop()
			{
				if (charging && emitting) { simulation.StopGather(); }
				emitting = false;
			}
			void Retire()
			{
				if (effect) {
					if (effect->particleObject) { effect->particleObject->SetAppCulled(true); }
					effect->age = effect->lifetime + 1.f;
				}
			}
		};
		std::vector<std::unique_ptr<Stream>> streams;

		void CollectPlayerGraphs(RE::BSAnimationGraphManager* manager,
			std::vector<RE::BShkbAnimationGraphPtr>& result, RE::BShkbAnimationGraph*& active)
		{
#include "BowGraphSnapshot.inc"
		}

		unsigned RootDistance(RE::NiAVObject* graphRoot, RE::NiAVObject* visibleRoot)
		{
			if (!graphRoot || !visibleRoot) { return 1000; }
			for (unsigned distance = 0; graphRoot && distance < 64; ++distance, graphRoot = graphRoot->parent) {
				if (graphRoot == visibleRoot) { return distance; }
			}
			return 1000;
		}

		void RefreshPlayerGraphs(RE::PlayerCharacter* player)
		{
			RE::BSAnimationGraphManagerPtr manager;
			if (player) { player->GetAnimationGraphManager(manager); }
			std::vector<RE::BShkbAnimationGraphPtr> current;
			RE::BShkbAnimationGraph* active = nullptr;
			CollectPlayerGraphs(manager.get(), current, active);
			auto* camera = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = camera && camera->IsInFirstPerson();
			auto* root = player ? player->Get3D(firstPerson) : nullptr;
			RE::BShkbAnimationGraph* selected = active;
			unsigned bestDistance = 1000;
			for (const auto& graph : current) {
				const auto distance = RootDistance(graph->rootNode, root);
				if (distance < bestDistance || (distance == bestDistance && graph.get() == active)) {
					bestDistance = distance;
					selected = graph.get();
				}
			}
			const auto source = selected ? reinterpret_cast<std::uintptr_t>(selected->GetEventSource<RE::BSAnimationGraphEvent>()) : 0;
			if (playerView.Refresh(firstPerson, reinterpret_cast<std::uintptr_t>(root), source)) {
				playerViewEpoch.store(playerView.epoch);
				playerEventSource.store(source);
				if (player) {
					if (auto state = chargeStates.find(player->GetHandle()); state != chargeStates.end()) { state->second.OnViewChanged(); }

					for (auto& stream : streams) { if (stream->charging && stream->actor == player->GetHandle()) { stream->Stop(); } }
				}
			}

			for (const auto& graph : playerGraphs) {
				if (std::none_of(current.begin(), current.end(), [&](const auto& next) { return next.get() == graph.get(); })) {
					graph->RemoveEventSink<RE::BSAnimationGraphEvent>(&playerAnimationSink);
				}
			}
			for (const auto& graph : current) { graph->AddEventSink<RE::BSAnimationGraphEvent>(&playerAnimationSink); }
			playerGraphs = std::move(current);
		}

		bool Allowed(RE::Actor* actor);

		RE::InventoryEntryData* BowEntry(RE::Actor* actor, RE::FormID weaponID)
		{
			for (bool left : { false, true }) {
				if (auto* entry = actor->GetEquippedEntryData(left); entry && entry->object) {
					if (auto* weapon = entry->object->As<RE::TESObjectWEAP>(); weapon && (!weaponID || weapon->GetFormID() == weaponID) &&
						(weapon->IsBow() || weapon->IsCrossbow())) { return entry; }
				}
			}
			return nullptr;
		}

		RE::NiColorA Color(RE::Actor* actor, RE::FormID weaponID)
		{
			if (!weaponID) { return { 0.025f, 0.080f, 0.650f, 1.f }; }

			AttackTrail selector(nullptr, actor->GetHandle(), nullptr, nullptr, false, false);
			TrailDefinition definition;
			auto* entry = BowEntry(actor, weaponID);
			if (selector.GetTrailDefinition(actor->GetHandle(), entry, entry == actor->GetEquippedEntryData(true), definition) && definition.trailOverride.baseColorOverride) {
				return *definition.trailOverride.baseColorOverride;
			}
			return { 0.025f, 0.080f, 0.650f, 1.f };
		}

		RE::TESObjectWEAP* EquippedBow(RE::Actor* actor)
		{
			for (bool left : { false, true }) {
				if (auto* form = actor->GetEquippedObject(left)) {
					if (auto* weapon = form->As<RE::TESObjectWEAP>(); weapon && weapon->IsBow()) { return weapon; }
				}
			}
			return nullptr;
		}

		bool ChargeTip(RE::Actor* actor, RE::TESObjectWEAP* weapon, OrderedChargeState& state, float deltaTime, Vec3& tip)
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			const bool firstPerson = actor->IsPlayerRef() && camera && camera->IsInFirstPerson();
			auto* root = actor->Get3D(firstPerson);
			if (!root) { return false; }

			auto* node = root->GetObjectByName("Arrow:0"sv);
			if (!node) { node = root->GetObjectByName("Arrow"sv); }
			std::optional<bool> graphAttacking, graphDrawn;
			bool value = false;
			if (actor->IsPlayerRef()) {
				for (const auto& graph : playerGraphs) {
					if (reinterpret_cast<std::uintptr_t>(graph->GetEventSource<RE::BSAnimationGraphEvent>()) == playerView.source) {
						if (graph->GetGraphVariableBool("IsAttacking"sv, value) || graph->GetGraphVariableBool("bIsAttacking"sv, value)) { graphAttacking = value; }
						if (graph->GetGraphVariableBool("bBowDrawn"sv, value)) { graphDrawn = value; }
						break;
					}
				}
			} else {
				if (actor->GetGraphVariableBool("IsAttacking"sv, value) || actor->GetGraphVariableBool("bIsAttacking"sv, value)) { graphAttacking = value; }
				if (actor->GetGraphVariableBool("bBowDrawn"sv, value)) { graphDrawn = value; }
			}
			const auto nativeState = static_cast<std::uint32_t>(actor->AsActorState()->GetAttackState());
			const bool nocked = node && !node->GetAppCulled();
			const bool active = state.ActiveOrdered(nativeState, graphAttacking, nocked, deltaTime, graphDrawn, eventSequence.fetch_add(1) + 1);
			if (diagnosticCapture.load(std::memory_order_relaxed) && actor->IsPlayerRef()) {
				chargeDiagnostics = { nativeState, graphAttacking ? (*graphAttacking ? 1 : 0) : -1,
					graphDrawn ? (*graphDrawn ? 1 : 0) : -1, true, nocked, active };
			}
			if (!active) { return false; }
			if (node && node->GetAppCulled()) { node = nullptr; }
			if (!node) { node = weapon->GetFireNode(root); }
			const bool estimateFromHand = !node;
			if (!node) { node = root->GetObjectByName("NPC L Hand [LHnd]"sv); }
			if (!node) { return false; }
			tip = ToVec(node->world.translate);
			if (estimateFromHand) {
				const float yaw = actor->GetAimHeading(), pitch = actor->GetAimAngle();
				tip = tip + Vec3{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) } * 24.f;
			}
			return tip.Finite();
		}

		bool NearPlayer(Vec3 point)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && point.Finite() && (point - ToVec(player->GetPosition())).Length() < 6000.f;
		}

		void DisableControllers(RE::NiAVObject* object)
		{
			for (auto* controller = object->GetControllers(); controller; controller = controller->GetNext()) {
				controller->flags.reset(RE::NiTimeController::Flag::kActive);
			}
			if (auto* node = object->AsNode()) {
				for (auto& child : node->children) { if (child) { DisableControllers(child.get()); } }
			}
		}

#include "BowRenderer.inc"

		struct RendererFrameSnapshot
		{
			RE::NiBound model, world, root, data;
			RE::NiTransform transform;
			Vec3 nativeFirst;
			std::uint16_t count = 0, submitted = 0;
			unsigned hidden = 0, ancestors = 0, activeControllers = 0, controllers = 0, activeModifiers = 0, modifiers = 0;
			bool reachesRoot = false, finite = true;
			RE::NiAVObject* firstHidden = nullptr;
			RE::BSEffectShaderProperty* shader = nullptr;
			RE::BSEffectShaderMaterial* material = nullptr;
			float nativeAlphaMin = 1.f, nativeAlphaMax = 0.f, cpuAlphaMax = 0.f;
			unsigned nativeSampled = 0, cpuSampled = 0, invalidPositions = 0;
			std::uint32_t rootFlags = 0, systemFlags = 0;
			float rootFade = -1.f, rootFadeAmount = -1.f;
			bool rootCulled = false, systemCulled = false;
		};

		std::optional<RendererFrameSnapshot> CaptureRendererFrame(Stream& stream, bool diagnostics)
		{
			if (!RendererDiagnosticDue<std::chrono::steady_clock>(diagnostics, stream.nextRendererDiagnostic)) { return std::nullopt; }
			auto* system = stream.system.get();
			auto* root = stream.effect->particleObject.get();
			auto* data = system->GetParticlesRuntimeData().particleData.get();
			const auto& vertices = data->GetParticlesRuntimeData();
			RendererFrameSnapshot frame;
			frame.model = system->GetModelData().modelBound;
			frame.world = system->worldBound;
			frame.root = root->worldBound;
			frame.data = vertices.bound;
			frame.transform = system->world;
			frame.count = vertices.numVertices;
			frame.submitted = stream.rendererSubmittedCount;
			frame.rootFlags = root->GetFlags().underlying();
			frame.systemFlags = system->GetFlags().underlying();
			frame.rootCulled = root->GetAppCulled();
			frame.systemCulled = system->GetAppCulled();
			if (auto* fade = root->AsFadeNode()) { frame.rootFade = fade->currentFade; }
			frame.rootFadeAmount = root->fadeAmount;
			frame.finite = ToVec(frame.transform.translate).Finite() && std::isfinite(frame.transform.scale);
			for (const auto& row : frame.transform.rotate.entry) {
				for (const float value : row) { frame.finite = frame.finite && std::isfinite(value); }
			}
			if (frame.count && vertices.positions) { frame.nativeFirst = ToVec(frame.transform * vertices.positions[0]); }
			frame.nativeSampled = vertices.color ? std::min<unsigned>({ frame.count, vertices.maxNumVertices, 32 }) : 0;
			for (unsigned i = 0; i < frame.nativeSampled; ++i) {
				const auto index = i * std::min<unsigned>(frame.count, vertices.maxNumVertices) / frame.nativeSampled;
				frame.nativeAlphaMin = std::min(frame.nativeAlphaMin, vertices.color[index].alpha);
				frame.nativeAlphaMax = std::max(frame.nativeAlphaMax, vertices.color[index].alpha);
				frame.invalidPositions += !ToVec(vertices.positions[index]).Finite();
			}
			const auto& particles = stream.simulation.Particles();
			frame.cpuSampled = static_cast<unsigned>(std::min<std::size_t>(particles.size(), 32));
			for (unsigned i = 0; i < frame.cpuSampled; ++i) {
				frame.cpuAlphaMax = std::max(frame.cpuAlphaMax, particles[i * particles.size() / frame.cpuSampled].Alpha());
			}
			for (auto* object = static_cast<RE::NiAVObject*>(system); object && frame.ancestors < 32; object = object->parent) {
				++frame.ancestors;
				if (object->GetAppCulled()) { ++frame.hidden; if (!frame.firstHidden) { frame.firstHidden = object; } }
				for (auto* controller = object->GetControllers(); controller && frame.controllers < 32; controller = controller->GetNext()) {
					++frame.controllers;
					frame.activeControllers += controller->flags.any(RE::NiTimeController::Flag::kActive);
				}
				if (object == root) { frame.reachesRoot = true; break; }
			}
			for (const auto& modifier : system->GetParticleSystemRuntimeData().modifierList) {
				if (frame.modifiers >= 32) { break; }
				++frame.modifiers;
				frame.activeModifiers += modifier && modifier->active;
			}
			frame.shader = netimmerse_cast<RE::BSEffectShaderProperty*>(system->GetGeometryRuntimeData().shaderProperty.get());
			frame.material = frame.shader ? skyrim_cast<RE::BSEffectShaderMaterial*>(frame.shader->material) : nullptr;
			return frame;
		}

		void ReportRendererFrame(const Stream& stream, const RendererFrameSnapshot& frame)
		{
			auto* system = stream.system.get();
			auto* root = stream.effect->particleObject.get();
			auto* data = system->GetParticlesRuntimeData().particleData.get();
			const auto& vertices = data->GetParticlesRuntimeData();
			const auto& runtime = system->GetParticleSystemRuntimeData();
			auto* material = frame.material;
			auto* fade = root->AsFadeNode();
			auto* shaderFade = frame.shader ? frame.shader->fadeNode : nullptr;
			const Vec3 afterFirst = vertices.numVertices ? ToVec(system->world * vertices.positions[0]) : Vec3{};
			RedsandLog::info("Redsand bow frame: projectile={:08X} charging={} emitting={} stream={} effect={} root={} system={} data={} rendererData={} attached={} ancestry={}/{} hidden={} firstHidden={} flags={:X}/{:X} culled={}/{} afterCulled={}/{} age={}/{} cpu={} submitted={} native={}/{} max={} sampled={}/{} nativeAlpha={}/{} cpuAlphaMax={} first={}/{}/{} expectedFirst={}/{}/{} afterFirst={}/{}/{} world={}/{}/{} scale={} finiteWorld={} invalidPositions={} dataBound={}/{}/{}/{} modelBound={}/{}/{}/{} worldBound={}/{}/{}/{} rootBound={}/{}/{}/{} afterModelRadius={} afterWorldRadius={} afterRootRadius={} controllers={}/{} modifiers={}/{} nativeTime={} nativeFrame={} renderFrame={} renderPassState={} passes={} reset={} dynamicBounds={} worldspace={} shader={} material={} shaderAlpha={} materialAlpha={} materialScale={} texture={} rootFade={}/{} fadeAmount={} shaderFade={} fade={}",
				stream.projectile.native_handle(), stream.charging, stream.emitting, fmt::ptr(&stream), fmt::ptr(stream.effect.get()),
				fmt::ptr(root), fmt::ptr(system), fmt::ptr(data), fmt::ptr(system->GetGeometryRuntimeData().rendererData),
				root->parent != nullptr, frame.ancestors, frame.reachesRoot, frame.hidden, fmt::ptr(frame.firstHidden),
				frame.rootFlags, frame.systemFlags, frame.rootCulled, frame.systemCulled, root->GetAppCulled(), system->GetAppCulled(),
				stream.effect->age, stream.effect->lifetime, stream.simulation.Particles().size(), frame.submitted, frame.count,
				vertices.numVertices, vertices.maxNumVertices, frame.nativeSampled, frame.cpuSampled,
				frame.nativeSampled ? frame.nativeAlphaMin : -1.f, frame.nativeSampled ? frame.nativeAlphaMax : -1.f, frame.cpuAlphaMax,
				frame.nativeFirst.x, frame.nativeFirst.y, frame.nativeFirst.z,
				stream.rendererSubmittedFirst.x, stream.rendererSubmittedFirst.y, stream.rendererSubmittedFirst.z,
				afterFirst.x, afterFirst.y, afterFirst.z, frame.transform.translate.x, frame.transform.translate.y, frame.transform.translate.z,
				frame.transform.scale, frame.finite, frame.invalidPositions, frame.data.center.x, frame.data.center.y, frame.data.center.z, frame.data.radius,
				frame.model.center.x, frame.model.center.y, frame.model.center.z, frame.model.radius,
				frame.world.center.x, frame.world.center.y, frame.world.center.z, frame.world.radius,
				frame.root.center.x, frame.root.center.y, frame.root.center.z, frame.root.radius,
				system->GetModelData().modelBound.radius, system->worldBound.radius, root->worldBound.radius,
				frame.activeControllers, frame.controllers, frame.activeModifiers, frame.modifiers, runtime.lastUpdate,
				system->lastUpdatedFrameCounter, frame.shader ? frame.shader->lastAccumulatedFrameCount : 0,
				frame.shader ? frame.shader->lastRenderPassState : -1, fmt::ptr(frame.shader ? frame.shader->renderPassList.head : nullptr),
				runtime.resetSystem, runtime.dynamicBounds, runtime.isWorldspace, fmt::ptr(frame.shader), fmt::ptr(material),
				frame.shader ? frame.shader->alpha : -1.f, material ? material->baseColor.alpha : -1.f,
				material ? material->baseColorScale : -1.f, material ? material->sourceTexturePath.c_str() : "unavailable",
				frame.rootFade, fade ? fade->currentFade : -1.f, frame.rootFadeAmount, fmt::ptr(shaderFade), shaderFade ? shaderFade->currentFade : -1.f);
		}

		bool Render(Stream& stream, float brightness, bool diagnostics)
		{
			const auto renderer = PrepareRenderer(stream, brightness, diagnostics);
			if (renderer != RendererStatus::Ready) { return renderer == RendererStatus::Pending; }
			auto* system = stream.system.get();
			auto* data = system->GetParticlesRuntimeData().particleData.get();
			if (!data || !std::isfinite(system->world.scale) || system->world.scale <= 0.0001f) { return false; }
			const auto frame = CaptureRendererFrame(stream, diagnostics);
			auto& vertices = data->GetParticlesRuntimeData();
			auto& runtime = data->GetPSysRuntimeData();
			const auto& particles = stream.simulation.Particles();
			const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(particles.size(), vertices.maxNumVertices));
			const auto inverse = system->world.Invert();
			RE::NiPoint3 low{ 0.f, 0.f, 0.f }, high = low;
			for (std::uint16_t i = 0; i < count; ++i) {
				const auto& p = particles[i];
				const auto position = inverse * ToNi(p.position);
				vertices.positions[i] = position;
				vertices.color[i] = RE::NiColorA{ 1.f, 1.f, 1.f, p.Alpha() };
				vertices.radii[i] = p.Size() / system->world.scale;
				vertices.sizes[i] = 1.f;
				if (vertices.rotationAngles) { vertices.rotationAngles[i] = 0.f; }
				if (vertices.rotationAxes) { vertices.rotationAxes[i] = { 0.f, 0.f, 1.f }; }
				if (vertices.rotations) { vertices.rotations[i] = RE::NiQuaternion{ 1.f, 0.f, 0.f, 0.f }; }
				if (vertices.textureIndices) { vertices.textureIndices[i] = 0; }
				if (runtime.rotationSpeeds) { runtime.rotationSpeeds[i] = 0.f; }
				if (runtime.particleInfo) {
					runtime.particleInfo[i] = {};
					runtime.particleInfo[i].age = p.age;
					runtime.particleInfo[i].lifespan = p.lifetime;
				}
				if (i == 0) { low = high = position; }
				low.x = std::min(low.x, position.x); low.y = std::min(low.y, position.y); low.z = std::min(low.z, position.z);
				high.x = std::max(high.x, position.x); high.y = std::max(high.y, position.y); high.z = std::max(high.z, position.z);
			}
			vertices.numVertices = count;
			runtime.addedParticleCount = 0;
			runtime.addedParticlesBase = 0;
			vertices.bound.center = (low + high) * 0.5f;
			vertices.bound.radius = (high - low).Length() * 0.5f + 6.f / system->world.scale;
			if (!SubmitRendererVisibility(stream, count)) { return false; }
			system->GetModelData().modelBound = vertices.bound;
			system->UpdateWorldBound();
			for (auto* parent = system->parent; parent; parent = parent->parent) {
				parent->UpdateWorldBound();
				if (parent == stream.effect->particleObject.get()) { break; }
			}
			if (auto* fade = stream.effect->particleObject->AsFadeNode()) { fade->currentFade = 1.f; }
			if (frame) { ReportRendererFrame(stream, *frame); }
			if (diagnostics) {
				stream.rendererSubmittedCount = count;
				stream.rendererSubmittedFirst = count ? particles.front().position : Vec3{};
			}
			return true;
		}

		Stream* CreateStream(RE::Actor* actor, RE::TESObjectCELL* cell, Vec3 point, std::uint32_t seed, RE::FormID weaponID = 0)
		{
			if (!cell || !Allowed(actor)) { return nullptr; }
			weaponID = Redsand::Weapons::BowVisualWeaponID(weaponID, rapidComboWeapon.load(std::memory_order_relaxed), [&] {
				auto* equipped = EquippedBow(actor);
				return equipped ? equipped->GetFormID() : 0;
			});
			const auto profile = Redsand::Weapons::ForWeapon(RE::TESForm::LookupByID<RE::TESObjectWEAP>(weaponID));

			static const std::string meshes[] = {
				Redsand::Weapons::MapMeshPath(particleMesh, Redsand::Weapons::Profile::Default),
				Redsand::Weapons::MapMeshPath(particleMesh, Redsand::Weapons::Profile::Bow),
				Redsand::Weapons::MapMeshPath(particleMesh, Redsand::Weapons::Profile::Crossbow)
			};
			const auto& mesh = meshes[profile == Redsand::Weapons::Profile::Bow ? 1 : profile == Redsand::Weapons::Profile::Crossbow ? 2 : 0];
			Stream* recycled = nullptr;
			if (streams.size() >= maxStreams) {
				const auto reusable = Redsand::Bow::StreamToReuse(streams, maxStreams, [&](const Stream& stream) {
					return stream.meshPath == mesh && stream.effect && stream.effect->cell == cell &&
						stream.effect->age <= stream.effect->lifetime &&
						!(stream.rendererWasAttached && stream.effect->particleObject && !stream.effect->particleObject->parent);
				});
				if (reusable != streams.size()) {
					recycled = streams[reusable].get();
					Redsand::Bow::ForgetRendererLease(shots, *recycled);
					Redsand::Bow::ResetStreamLease(*recycled, seed);
					Redsand::Bow::NewestLeaseAtBack(streams, reusable);
				} else {
					const auto victim = Redsand::Bow::StreamToRetire(streams, maxStreams);
					if (victim == streams.size()) { return nullptr; }
					Redsand::Bow::ForgetRendererLease(shots, *streams[victim]);
					streams[victim]->Retire();
					streams.erase(streams.begin() + victim);
				}
			}
			auto owned = recycled ? std::unique_ptr<Stream>{} : std::make_unique<Stream>(seed);
			auto* stream = recycled ? recycled : owned.get();
			stream->actor = actor->GetHandle();
			stream->tip = point;
			stream->color = Color(actor, weaponID);
			stream->weaponID = weaponID;
			stream->simulation.SetStyle(profile == Redsand::Weapons::Profile::Bow ? Redsand::Bow::TrailStyle::VanillaBow :
				profile == Redsand::Weapons::Profile::Crossbow ? Redsand::Bow::TrailStyle::VanillaCrossbow : Redsand::Bow::TrailStyle::Default);
			stream->meshPath = mesh;
			if (!stream->effect) { stream->effect.reset(RE::BSTempEffectParticle::Spawn(cell, 7.f, mesh.c_str(), RE::NiMatrix3{}, ToNi(point), 1.f, 7, nullptr)); }
			if (!stream->effect) { return nullptr; }
			if (owned) { streams.push_back(std::move(owned)); }
			return stream;
		}

		void ObserveCharge(RE::Actor* actor, float density, float deltaTime)
		{
			auto* weapon = EquippedBow(actor);
			if (!weapon || actor->AsActorState()->GetWeaponState() != RE::WEAPON_STATE::kDrawn || !Allowed(actor)) { return; }
			const auto handle = actor->GetHandle();
			if (!chargeStates.contains(handle) && chargeStates.size() >= 128) { return; }
			auto& state = chargeStates[handle];
			state.seen = true;
			Vec3 tip;
			if (!ChargeTip(actor, weapon, state, deltaTime, tip) || !NearPlayer(tip)) { return; }
			Stream* stream = nullptr;
			for (auto& candidate : streams) {
				if (candidate->charging && candidate->emitting && candidate->actor == actor->GetHandle()) {
					if (candidate->weaponID != weapon->GetFormID() || candidate->effect->cell != actor->GetParentCell() || (candidate->tip - tip).Length() > 512.f) { candidate->Stop(); }
					else { stream = candidate.get(); }
					break;
				}
			}
			if (!stream) {
				stream = CreateStream(actor, actor->GetParentCell(), tip, actor->GetFormID(), weapon->GetFormID());
				if (!stream) { return; }
				stream->charging = true;
				stream->simulation.BeginGather(tip, density);
			}
			stream->tip = tip;
			stream->chargeSequence = state.DrawSequence();
			stream->seen = true;
		}

#include "BowArrowProcessing.inc"
#include "BowMotionDiagnostics.inc"

		void LogBowHealth(const RedsandSettings::Config& configuration, bool display, bool paused, RE::PlayerCharacter* player)
		{
			if (!configuration.diagnostics) { nextDiagnostic = {}; return; }
			const auto now = std::chrono::steady_clock::now();
			if (now < nextDiagnostic) { return; }
			nextDiagnostic = now + std::chrono::seconds(1);
			LogPlayerMotion(player, paused);
			std::size_t queued = 0, reserved = 0;
			std::uint64_t dropped = 0, merged = 0;
			{
				std::lock_guard lock(sampleLock);
				queued = pendingSamples.Size();
				reserved = birthReservations.Count();
				dropped = pendingSamples.Dropped();
				merged = pendingSamples.Merged();
			}
			std::size_t particles = 0, charging = 0, readyRenderers = 0;
			for (const auto& stream : streams) {
				particles += stream->simulation.Particles().size();
				charging += stream->charging && stream->emitting;
				readyRenderers += stream->system && stream->effect && stream->effect->particleObject;
			}
			const auto found = player ? chargeStates.find(player->GetHandle()) : chargeStates.end();
			const OrderedChargeState empty;
			const auto& state = found == chargeStates.end() ? empty : found->second;
			RedsandLog::info("Redsand bow health: enabled={} display={} paused={} bow={} gather={} rings={} trails={} density={} brightness={} calls={} captured={} missingWeapon={} unsupportedWeapon={} reservationRejected={} samples={} births={} queued={} dropped={} merged={} reservations={} shots={} streams={} charging={} readyRenderers={} particles={}",
				enabled.load(), display, paused, configuration.bowEnabled, configuration.bowGathering, configuration.bowRings,
				configuration.bowTrails, configuration.bowDensity, configuration.bowBrightness, captureDiagnostics.calls.load(),
				captureDiagnostics.captured.load(), captureDiagnostics.missingWeapon.load(), captureDiagnostics.unsupportedWeapon.load(),
				captureDiagnostics.reservationRejected.load(), diagnosticSamples, diagnosticBirths, queued, dropped, merged,
				reserved, shots.size(), streams.size(), charging, readyRenderers, particles);
			RedsandLog::info("Redsand bow charge: observed={} actor={:08X} weaponState={} native={} attacking={} drawn={} nocked={} active={} stopped={} attached={} full={} eventReady={} pulling={} restart={} ready={} graphs={} source={:X} epoch={} events={} dropped={} sourceRejected={}",
				chargeDiagnostics.observed, player ? player->GetFormID() : 0, player ? static_cast<std::uint32_t>(player->AsActorState()->GetWeaponState()) : 0,
				chargeDiagnostics.native, chargeDiagnostics.attacking, chargeDiagnostics.drawn, chargeDiagnostics.nocked,
				chargeDiagnostics.active, state.stopped, state.attached, state.fullyDrawn, state.eventReady, state.pulling,
				state.nativeRestartPending, state.readyTime, playerGraphs.size(), playerView.source, playerView.epoch,
				captureDiagnostics.chargeEvents.load(), captureDiagnostics.chargeDropped.load(), captureDiagnostics.sourceRejected.load());
			RedsandLog::info("Redsand bow renderer: expired={} detached={} unavailable={} timeouts={} recovered={}",
				rendererDiagnostics.expired, rendererDiagnostics.detached, rendererDiagnostics.unavailable, rendererDiagnostics.timeout, rendererDiagnostics.recovered);
		}

		void ArrowUpdate(RE::ArrowProjectile* arrow, float delta);
		REL::Relocation<decltype(ArrowUpdate)> originalArrowUpdate;

		void ArrowUpdate(RE::ArrowProjectile* arrow, float delta)
		{
			const auto sampleGeneration = generation.load();
			auto& data = arrow->GetProjectileRuntimeData();

			const auto proxyID = rapidComboWeapon.load(std::memory_order_relaxed);
			const bool rapidComboShot = proxyID && data.weaponSource && data.weaponSource->GetFormID() == proxyID;
			const bool diagnostics = diagnosticCapture.load(std::memory_order_relaxed);
			if (diagnostics) {
				captureDiagnostics.calls.fetch_add(1, std::memory_order_relaxed);
				if (!data.weaponSource) { captureDiagnostics.missingWeapon.fetch_add(1, std::memory_order_relaxed); }
				else if (!data.weaponSource->IsBow() && !data.weaponSource->IsCrossbow() && !rapidComboShot) {
					captureDiagnostics.unsupportedWeapon.fetch_add(1, std::memory_order_relaxed);
				}
			}
			bool capture = enabled.load(std::memory_order_relaxed) && std::isfinite(delta) && delta > 0.f &&
				data.weaponSource && (data.weaponSource->IsBow() || data.weaponSource->IsCrossbow() || rapidComboShot) && arrow->GetMissileRuntimeData().impactResult == RE::ImpactResult::kNone &&
				!data.flags.any(RE::Projectile::Flags::kDestroyed, RE::Projectile::Flags::kProcessedImpacts);
			ArrowSample sample;
			if (capture) {
				std::lock_guard lock(sampleLock);
				capture = enabled.load(std::memory_order_relaxed) && sampleGeneration == generation.load();
				if (capture) {
					sample.sequence = eventSequence.fetch_add(1) + 1;
					sample.handle = RE::ProjectileHandle{ arrow };
					sample.livingTime = data.livingTime;
					capture = std::isfinite(sample.livingTime) && sample.livingTime >= 0.f;
					if (capture && sample.livingTime <= 0.25f && !birthReservations.Begin(sample.handle)) {
						capture = false;
						if (diagnostics) { captureDiagnostics.reservationRejected.fetch_add(1, std::memory_order_relaxed); }
					}
					if (capture) {
						if (diagnostics) { captureDiagnostics.captured.fetch_add(1, std::memory_order_relaxed); }
						sample.shooter = data.shooter;
						sample.weaponID = data.weaponSource->GetFormID();
						if (auto* cell = arrow->GetParentCell()) { sample.cellID = cell->GetFormID(); }
						sample.from = ToVec(arrow->GetPosition());
						sample.direction = ToVec(data.velocity);
						sample.delta = delta;
						sample.power = data.power;
					}
				}
			}
			originalArrowUpdate(arrow, delta);
			if (capture) {
				sample.to = ToVec(arrow->GetPosition());
				const float distance = (sample.to - sample.from).Length();
				const float expected = sample.direction.Length() * sample.delta;

				sample.continuous = distance <= 2048.f || (std::isfinite(expected) && distance <= expected * 1.5f + 128.f);
				if (sample.direction.Length() < 0.001f) { sample.direction = sample.to - sample.from; }
				sample.flying = arrow->GetMissileRuntimeData().impactResult == RE::ImpactResult::kNone &&
					!data.flags.any(RE::Projectile::Flags::kDestroyed, RE::Projectile::Flags::kProcessedImpacts);

				std::lock_guard lock(sampleLock);
				if (sampleGeneration == generation.load()) {
					if (enabled.load(std::memory_order_relaxed)) { pendingSamples.Push(sample); }
					if (sample.livingTime <= 0.25f) { birthReservations.End(sample.handle); }
				}
			}
		}


	}

	void Install()
	{

		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_ArrowProjectile[0] };
		originalArrowUpdate = vtable.write_vfunc(0xAB, ArrowUpdate);
		RedsandLog::info("Redsand bow particles v17: runtime-aware actor state and read-only motion diagnostics");
	}

	void OnAnimationEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
	{

		if (event && event->holder && !event->holder->IsPlayerRef()) { ProcessAnimationEvent(event, source); }
	}

	void SetEnabled(bool value)
	{
		enabled.store(false);
		generation.fetch_add(1);
		playerEventSource.store(0);
		for (const auto& graph : playerGraphs) { graph->RemoveEventSink<RE::BSAnimationGraphEvent>(&playerAnimationSink); }
		playerGraphs.clear();
		playerView = {};
		playerViewEpoch.store(0);
		{
			std::lock_guard lock(sampleLock);
			pendingSamples.Clear();
			birthReservations.Clear();
			batchBirths.clear();
			pendingChargeEvents.clear();
		}
		for (auto& stream : streams) { stream->Retire(); }
		streams.clear();
		chargeStates.clear();
		shots.clear();
		rapidComboWeapon.store(0);
		diagnosticCapture.store(false, std::memory_order_relaxed);
		motionTrace.Enable(false);
		captureDiagnostics.calls = captureDiagnostics.captured = captureDiagnostics.missingWeapon = 0;
		captureDiagnostics.unsupportedWeapon = captureDiagnostics.reservationRejected = 0;
		captureDiagnostics.chargeEvents = captureDiagnostics.chargeDropped = captureDiagnostics.sourceRejected = 0;
		chargeDiagnostics = {};
		diagnosticSamples = diagnosticBirths = 0;
		nextDiagnostic = {};

		if (value) {
			if (auto* proxy = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(0x82F, "BowRapidCombo.esp")) {
				rapidComboWeapon.store(proxy->GetFormID());
				RedsandLog::info("Redsand bow particles: Bow Rapid Combo compatibility enabled, Dummy_Bow={:08X}", proxy->GetFormID());
			}
			static ActionSink sink;
			static bool registered = false;
			if (!registered) {
				if (auto* source = SKSE::GetActionEventSource()) { source->AddEventSink(&sink); registered = true; }
			}
			static MotionInputSink motionSink;
			static bool motionRegistered = false;
			if (!motionRegistered) {
				if (auto* source = SKSE::GetModCallbackEventSource()) { source->AddEventSink(&motionSink); motionRegistered = true; }
			}
		}
		enabled.store(value);
	}

	void Update(float deltaTime)
	{
		if (!enabled.load()) { return; }
		const auto configuration = RedsandSettings::Get();
		if (diagnosticCapture.exchange(configuration.diagnostics, std::memory_order_relaxed) != configuration.diagnostics) {
			motionTrace.Enable(configuration.diagnostics);
		}
		if (!std::isfinite(deltaTime) || deltaTime <= 0.f) { return; }
		chargeDiagnostics.observed = false;
		auto* ui = RE::UI::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const bool display = Settings::bDisplayTrails && !Settings::bDisableMod;
		if (!ui || ui->GameIsPaused()) { LogBowHealth(configuration, display, true, player); return; }

		RefreshPlayerGraphs(player);
		std::vector<ArrowSample> samples;
		std::vector<ChargeEvent> chargeEvents;
		{
			std::lock_guard lock(sampleLock);
			DrainArrowSamples(samples);
			chargeEvents.swap(pendingChargeEvents);
		}
		for (auto& [handle, state] : chargeStates) { state.seen = false; }
		if (configuration.diagnostics) {
			diagnosticSamples += samples.size();
			for (const auto& sample : samples) { diagnosticBirths += sample.livingTime <= 0.25f; }
		}
		const float density = configuration.bowDensity;
		ApplyStreamConfiguration(configuration, display, [](const Stream& stream) { return stream.actor.get(); });
		for (auto& [handle, shot] : shots) { shot.silence += deltaTime; }
		for (auto& stream : streams) {
			stream->seen = false;
			stream->age += deltaTime;
			stream->silence += deltaTime;
			if (!display) { stream->Stop(); }
			if (!stream->charging) { stream->simulation.Advance(deltaTime, stream->tip); }
		}

		Redsand::Bow::ReplayEvents(chargeEvents, samples, [&](const auto& event) {
			if (player && event.actor == player->GetHandle() && !playerView.Accepts(event.source, event.viewEpoch)) {
				if (configuration.diagnostics) { captureDiagnostics.sourceRejected.fetch_add(1, std::memory_order_relaxed); }
				return;
			}
			if (chargeStates.contains(event.actor) || chargeStates.size() < 128) {
				chargeStates[event.actor].SignalOrdered(event.signal, event.sequence);
			}
		}, [&](const auto& sample) {
			if (display && configuration.bowEnabled) { ProcessArrow(sample, density, configuration.bowRings, configuration.bowTrails); }
		});
		batchBirths.clear();

		PruneSilentShots();
		if (display && configuration.bowEnabled && configuration.bowGathering) {
			if (player) { ObserveCharge(player, density, deltaTime); }
			if (auto* processes = RE::ProcessLists::GetSingleton()) {
				processes->ForEachHighActor([&](RE::Actor* actor) {
					if (actor != player) { ObserveCharge(actor, density, deltaTime); }
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}
		std::erase_if(chargeStates, [](const auto& entry) { return !entry.second.seen; });
		for (auto& stream : streams) {
			if (!display || (stream->charging && !stream->seen)) { stream->Stop(); }

			if (stream->charging) { stream->simulation.Advance(deltaTime, stream->tip); }
			if (stream->charging && stream->emitting) {
				stream->silence = 0.f;
				stream->chargeTime += deltaTime;
				stream->simulation.Gather(deltaTime, stream->tip, density, stream->chargeTime);
			}
		}
		std::erase_if(streams, [&](auto& stream) {
			const bool finished = StreamFinished(*stream);
			if (finished || !Render(*stream, configuration.bowBrightness, configuration.diagnostics)) {
				if (!finished) { RecoverableStreamLoss(*stream); }
				else { Redsand::Bow::ForgetRendererLease(shots, *stream); }
				stream->Retire();
				return true;
			}
			return false;
		});
		LogBowHealth(configuration, display, false, player);
	}
}
