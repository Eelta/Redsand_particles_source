#include "../source/RedsandBowEventState.h"
#include "../source/RedsandBowSimulation.h"
#include "../source/RedsandBowStreamBudget.h"
#include "../source/RedsandSettings.h"

#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RedsandSettings
{
	Config current;
	Config Get() { return current; }
}

namespace RedsandLog
{
	unsigned scopeReports = 0;
	template <class... Args> void info(Args&&...) { ++scopeReports; }
}


namespace RE
{
	using ActorHandle = std::uint32_t;
	using FormID = std::uint32_t;
	struct ProjectileHandle
	{
		std::uint32_t value = 0;
		std::uint32_t native_handle() const { return value; }
		bool operator==(const ProjectileHandle&) const = default;
	};
	struct Actor
	{
		ActorHandle handle = 1;
		bool allowed = true;
		bool player = true, loaded = true, dead = false;
		FormID equippedWeapon = 0;
		ActorHandle GetHandle() const { return handle; }
		FormID GetFormID() const { return handle; }
		bool IsPlayerRef() const { return player; }
		bool Is3DLoaded() const { return allowed && loaded; }
		bool IsDead() const { return dead; }
		template <class T> T* As() { return static_cast<T*>(this); }
	};
	struct ObjectRefHandle
	{
		Actor* actor = nullptr;
		Actor* get() const { return actor; }
	};
	struct TESObjectCELL {};
	struct TESForm
	{
		template <class T> static T* LookupByID(FormID id)
		{
			static T cell;
			return id == 1 ? &cell : nullptr;
		}
	};
	struct NiColorA { float red = 0.025f, green = 0.08f, blue = 0.65f, alpha = 1.f; };
}

template <> struct std::hash<RE::ProjectileHandle>
{
	std::size_t operator()(RE::ProjectileHandle handle) const { return handle.value; }
};

namespace
{
	using Redsand::Bow::ChargeSignal;
	using Redsand::Bow::Kind;
	using Redsand::Bow::Simulation;
	using Redsand::Bow::Vec3;
	constexpr std::size_t maxStreams = 32, maxQueuedSamples = 2048;
	struct ArrowSample
	{
		std::uint64_t sequence = 0;
		std::uint64_t lastSequence = 0;
		RE::ProjectileHandle handle;
		RE::ObjectRefHandle shooter;
		RE::FormID cellID = 1;
		RE::FormID weaponID = 0;
		Vec3 from, to{ 0.f, 30.f, 0.f }, direction{ 0.f, 1.f, 0.f };
		float delta = 1.f / 60.f, livingTime = 0.f, power = 1.f;
		float latestLivingTime = -1.f;
		bool flying = true, continuous = true;
		Redsand::Bow::FlightPath path;
	};
	struct Stream
	{
        std::string enchantmentKey = "none";
		Simulation simulation;
		RE::ActorHandle actor = 0;
		RE::ProjectileHandle projectile;
		RE::NiColorA color;
		RE::FormID weaponID = 0;
		Vec3 tip;
		float age = 0.f, chargeTime = 0.f, silence = 0.f;
		float rendererStartAge = 0.f, rendererBrightness = -1.f;
		std::uint64_t chargeSequence = 0;
		bool charging = false, emitting = true, seen = false;
		static inline unsigned retired = 0;
		void Retire() { ++retired; }
		explicit Stream(std::uint32_t seed) : simulation(seed) {}
		void Stop()
		{
			if (charging && emitting) { simulation.StopGather(); }
			emitting = false;
		}
	};
	std::vector<std::unique_ptr<Stream>> streams;
	std::unordered_map<RE::ActorHandle, Redsand::Bow::OrderedChargeState> chargeStates;
	std::unordered_map<RE::ProjectileHandle, Redsand::Bow::ShotState> shots;
	std::mutex sampleLock;
	Redsand::Bow::SampleQueue<ArrowSample> pendingSamples{ maxQueuedSamples };
	Redsand::Bow::BirthReservations<RE::ProjectileHandle> birthReservations{ maxQueuedSamples };
	std::unordered_set<RE::ProjectileHandle> batchBirths;
	RE::Actor actor;
	unsigned spawnAttempts = 0, spawnFailuresRemaining = 0;

	bool Allowed(RE::Actor* candidate);
	bool NearPlayer(Vec3 point) { return point.Finite() && point.Length() < 6000.f; }
	Stream* CreateStream(RE::Actor* shooter, RE::TESObjectCELL* cell, Vec3 point, std::uint32_t seed, RE::FormID weaponID = 0)
	{
		if (!cell || !Allowed(shooter)) { return nullptr; }
		Stream* recycled = nullptr;
		if (streams.size() >= maxStreams) {
			const auto reusable = Redsand::Bow::StreamToReuse(streams, maxStreams, [&](const Stream& stream) { return stream.weaponID == weaponID; });
			if (reusable != streams.size()) {
				recycled = streams[reusable].get();
				Redsand::Bow::ForgetRendererLease(shots, *recycled);
				Redsand::Bow::ResetStreamLease(*recycled, seed);
				Redsand::Bow::NewestLeaseAtBack(streams, reusable);
			} else {
				const auto victim = Redsand::Bow::StreamToRetire(streams, maxStreams);
				if (victim == streams.size()) { return nullptr; }
				Redsand::Bow::ForgetRendererLease(shots, *streams[victim]);
				streams.erase(streams.begin() + victim);
			}
		}
		if (!recycled) {
			++spawnAttempts;
			if (spawnFailuresRemaining) { --spawnFailuresRemaining; return nullptr; }
		}
		auto owned = recycled ? std::unique_ptr<Stream>{} : std::make_unique<Stream>(seed);
		auto* stream = recycled ? recycled : owned.get();
		stream->actor = shooter->GetHandle();
		stream->tip = point;
		stream->weaponID = weaponID;

		stream->simulation.SetStyle(weaponID == 0x123 ? Redsand::Bow::TrailStyle::VanillaBow :
			weaponID == 0x124 ? Redsand::Bow::TrailStyle::VanillaCrossbow : Redsand::Bow::TrailStyle::Default);
		if (owned) { streams.push_back(std::move(owned)); }
		return stream;
	}

RedsandSettings::Config BowConfiguration(RE::Actor*) { return RedsandSettings::Get(); }
RedsandSettings::Config StreamConfiguration(const Stream& stream, RedsandSettings::Config config) { RedsandSettings::ApplyBowAppearance(config, stream.enchantmentKey); return config; }
float StreamDensity(const Stream& stream, float density) { const auto config = StreamConfiguration(stream, RedsandSettings::Get()); return config.enchantments.Find(stream.enchantmentKey) ? config.bowDensity : density; }
#include "../source/BowArrowProcessing.inc"

	void Check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}
	void Reset()
	{
		streams.clear();
		chargeStates.clear();
		shots.clear();
		pendingSamples.Clear();
		birthReservations.Clear();
		batchBirths.clear();
		actor = {};
		RedsandSettings::current = {};
		RedsandLog::scopeReports = 0;
		Stream::retired = 0;
		spawnAttempts = spawnFailuresRemaining = 0;
	}
	ArrowSample Sample(unsigned projectile, std::uint64_t sequence)
	{
		ArrowSample sample;
		sample.handle = { projectile };
		sample.shooter = { &actor };
		sample.sequence = sequence;
		return sample;
	}
	void CompleteBirth(const ArrowSample& sample)
	{
		std::lock_guard lock(sampleLock);
		pendingSamples.Push(sample);
		birthReservations.End(sample.handle);
	}
	void ReplayPending()
	{
		std::vector<ArrowSample> samples;
		{
			std::lock_guard lock(sampleLock);
			DrainArrowSamples(samples);
		}
		struct Event { std::uint64_t sequence; };
		std::vector<Event> events;
		Redsand::Bow::ReplayEvents(events, samples, [](const auto&) {}, [](const auto& sample) { ProcessArrow(sample, 1.f); });
	}
	Stream* Find(unsigned projectile)
	{
		for (auto& stream : streams) {
			if (!stream->charging && stream->projectile.value == projectile) { return stream.get(); }
		}
		return nullptr;
	}
	std::size_t Count(const Stream& stream, Kind kind)
	{
		return std::count_if(stream.simulation.Particles().begin(), stream.simulation.Particles().end(),
			[kind](const auto& particle) { return particle.kind == kind; });
	}
	Stream* BeginCharge(std::uint64_t sequence)
	{
		auto& state = chargeStates[actor.GetHandle()];
		state.SignalOrdered(ChargeSignal::Start, sequence);
		state.SignalOrdered(ChargeSignal::Attach, sequence + 1);
		state.SignalOrdered(ChargeSignal::Ready, sequence + 2);
		auto* stream = CreateStream(&actor, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, 1);
		Check(stream != nullptr, "fixture could not create its charge stream");
		stream->charging = true;
		stream->chargeSequence = state.lastSequence;
		stream->simulation.BeginGather({}, 1.f);
		return stream;
	}

	void FullBudgetKeepsAcceptingShotsAndDoesNotReplayEvictedArrows()
	{
		Reset();
		for (unsigned projectile = 1; projectile <= 96; ++projectile) {
			for (auto& stream : streams) { stream->age += 0.1f; }
			ProcessArrow(Sample(projectile, projectile), 1.f);
			auto* current = Find(projectile);
			Check(current && Count(*current, Kind::Ring) == 384 && Count(*current, Kind::Trail) > 0,
				"rapid fire failed to create an independent ring and trail for a later shot");
			Check(streams.size() == std::min<std::size_t>(projectile, maxStreams), "rapid fire exceeded or lost its stream budget");
			if (projectile > maxStreams) {
				const auto evicted = projectile - static_cast<unsigned>(maxStreams);
				Check(Find(evicted) == nullptr, "the oldest flying stream was not evicted");
				const auto attempts = spawnAttempts;
				for (int repeat = 0; repeat < 3; ++repeat) {
					auto stale = Sample(evicted, 1000 + projectile * 3 + repeat);
					stale.livingTime = 0.1f;
					ProcessArrow(stale, 1.f);
				}
				Check(spawnAttempts == attempts && Find(evicted) == nullptr && Find(projectile) == current,
					"an evicted arrow replayed its ring or displaced a newer shot");
			}
		}
		Check(shots.size() == 96 && spawnAttempts == maxStreams, "shot turnover lost its tombstones or continuously respawned async renderers");
	}

	void FailedSpawnRetriesWithoutCancellingTheNextDraw()
	{
		Reset();
		auto* oldCharge = BeginCharge(1);
		spawnFailuresRemaining = 1;
		ProcessArrow(Sample(1, 10), 1.f);
		Check(Find(1) == nullptr && chargeStates.at(actor.GetHandle()).stopped && !oldCharge->emitting,
			"failed spawn did not consume the original release exactly once");
		Check(shots.at({ 1 }).releaseConsumed && !shots.at({ 1 }).streamCreated, "failed spawn lost its retry bookkeeping");
		auto* nextCharge = BeginCharge(20);
		for (unsigned repeat = 0; repeat < 3; ++repeat) {
			auto retry = Sample(1, 30 + repeat);
			retry.livingTime = 0.1f;
			ProcessArrow(retry, 1.f);
			const auto& state = chargeStates.at(actor.GetHandle());
			Check(!state.stopped && state.fullyDrawn && state.lastSequence == 22 && nextCharge->emitting,
				"retrying a failed spawn cancelled the next draw or its charge cloud");
		}
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 384 && shots.at({ 1 }).streamCreated,
			"spawn retry failed to produce exactly one release ring");
		ProcessArrow(Sample(2, 40), 1.f);
		Check(Find(2) && Count(*Find(2), Kind::Ring) == 384 && !nextCharge->emitting,
			"the next real shot did not create its own effect and end its own draw");
	}

	void OlderWorkerSampleCannotCancelANewerDraw()
	{
		Reset();
		auto* nextCharge = BeginCharge(100);
		ProcessArrow(Sample(1, 50), 1.f);
		const auto& state = chargeStates.at(actor.GetHandle());
		Check(!state.stopped && state.fullyDrawn && state.lastSequence == 102 && nextCharge->emitting,
			"a delayed worker sample cancelled the newer charge state or stream");
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 384, "late sample could not retain its own independent release effect");
		ProcessArrow(Sample(2, 110), 1.f);
		Check(state.stopped && !nextCharge->emitting && Find(2) && Find(2) != Find(1),
			"a newer release failed to stop its draw and create an independent stream");
	}

	void FirstReleaseCreatesItsChargeFence()
	{
		Reset();
		Check(chargeStates.empty(), "fixture unexpectedly has a charge state");
		ProcessArrow(Sample(1, 10), 1.f);
		Check(chargeStates.contains(actor.GetHandle()) && chargeStates.at(actor.GetHandle()).stopped &&
			chargeStates.at(actor.GetHandle()).lastSequence == 10, "first release failed to create a stopped charge state");
		ProcessArrow(Sample(1, 11), 1.f);
		Check(chargeStates.at(actor.GetHandle()).lastSequence == 10 && Count(*Find(1), Kind::Ring) == 384,
			"repeated projectile updates replayed the first release");
		ProcessArrow(Sample(2, 12), 1.f);
		Check(Find(2) && Find(2) != Find(1) && Count(*Find(2), Kind::Ring) == 384 &&
			chargeStates.at(actor.GetHandle()).lastSequence == 12, "the next arrow failed to create an independent release");
	}

	void SamplingGapsAndLongFlightsRemainRecoverable()
	{
		Reset();
		ProcessArrow(Sample(1, 1), 1.f);
		auto* stream = Find(1);
		stream->age = stream->silence = 0.35f;
		stream->simulation.Advance(0.35f, {});
		Check(!StreamFinished(*stream) && stream->emitting, "one long frame permanently terminated a flying arrow");
		const auto before = Count(*stream, Kind::Trail);
		auto resumed = Sample(1, 2);
		resumed.livingTime = 0.35f;
		ProcessArrow(resumed, 1.f);
		Check(Count(*stream, Kind::Trail) > before && stream->silence == 0.f,
			"resumed worker samples could not continue a flight after a sampling gap");
		stream->age = 25.f;
		Check(!StreamFinished(*stream), "a live slow arrow inherited the obsolete 20-second hard cutoff");
		resumed.flying = false;
		resumed.sequence = 3;
		ProcessArrow(resumed, 1.f);
		Check(!stream->emitting && !StreamFinished(*stream), "impact immediately deleted the surviving world-space tail");
		const auto atImpact = stream->simulation.Particles().size();
		resumed.flying = true;
		resumed.sequence = 4;
		ProcessArrow(resumed, 1.f);
		Check(stream->simulation.Particles().size() == atImpact && !stream->emitting, "late flying sample restarted an impacted arrow");
		stream->simulation.Advance(Simulation::maxLifetime + 0.1f, {});
		Check(StreamFinished(*stream), "impacted particles did not retire after draining");

		Reset();
		ProcessArrow(Sample(1, 1), 1.f);
		stream = Find(1);
		stream->silence = Simulation::maxLifetime + 0.1f;
		stream->simulation.Advance(stream->silence, {});
		Check(StreamFinished(*stream), "an unloaded arrow with no remaining particles leaked its renderer");
	}

	void RendererLossRebuildsOnlyTheRemainingTrail()
	{
		Reset();
		ProcessArrow(Sample(1, 10), 1.f);
		Check(Find(1) && shots.at({ 1 }).streamCreated, "fixture failed its original renderer");
		RecoverableStreamLoss(*Find(1));
		streams.clear();
		auto* laterDraw = BeginCharge(20);
		auto resumed = Sample(1, 30);
		resumed.livingTime = 1.5f;
		ProcessArrow(resumed, 1.f);
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 0 && Count(*Find(1), Kind::Trail) > 0,
			"renderer recovery was blocked by the launch tombstone or replayed its release ring");
		Check(laterDraw->emitting && chargeStates.at(actor.GetHandle()).lastSequence == 22,
			"renderer recovery cancelled the next draw");

		Reset();
		spawnFailuresRemaining = 1;
		ProcessArrow(Sample(1, 10), 1.f);
		resumed = Sample(1, 11);
		resumed.livingTime = 0.5f;
		ProcessArrow(resumed, 1.f);
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 0 && Count(*Find(1), Kind::Trail) > 0,
			"an admitted shot could not recover a delayed spawn beyond the launch window");

		Reset();
		auto* staleCloud = BeginCharge(1);
		staleCloud->silence = 0.5f;
		staleCloud->tip = { 1000.f, 0.f, 0.f };
		ProcessArrow(Sample(1, 10), 1.f);
		Check(!staleCloud->emitting && Find(1), "late release left its stale charge cloud emitting or blocked the new shot");
	}

	void LiveFeaturesDoNotReplayLaunches()
	{
		Reset();
		ProcessArrow(Sample(1, 1), 1.f, false, false);
		Check(!Find(1) && shots.at({ 1 }).releaseConsumed && shots.at({ 1 }).streamCreated,
			"disabled flight features still allocated an effect or left a pending release");
		auto resumed = Sample(1, 2);
		resumed.livingTime = 0.1f;
		ProcessArrow(resumed, 1.f, true, true);
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 0 && Count(*Find(1), Kind::Trail) > 0,
			"enabling effects mid-flight replayed a ring");
		ProcessArrow(Sample(2, 3), 2.f, true, false);
		Check(Find(2) && Count(*Find(2), Kind::Ring) == 768 && Count(*Find(2), Kind::Trail) == 0,
			"live ring/trail flags or 200-percent particle density were ignored");
		Find(2)->simulation.SetEnabledKinds(true, false, true);
		Check(Find(2)->simulation.Empty(), "turning rings off left existing rings visible");
		Find(1)->simulation.SetEnabledKinds(true, true, false);
		Check(Find(1)->simulation.Empty(), "turning trails off left existing trails visible");
	}

	void LaunchWeaponSurvivesEquipmentChangesAndRecovery()
	{
		Reset();
		auto vanilla = Sample(1, 1);
		vanilla.weaponID = 0x123;
		actor.equippedWeapon = 0x999;
		ProcessArrow(vanilla, 1.f);
		Check(Find(1) && Find(1)->weaponID == 0x123 && Find(1)->simulation.Style() == Redsand::Bow::TrailStyle::VanillaBow,
			"delayed vanilla arrow used the actor's newly equipped mod bow");
		RecoverableStreamLoss(*Find(1));
		streams.clear();
		vanilla.sequence = 2;
		vanilla.livingTime = 1.f;
		ProcessArrow(vanilla, 1.f);
		Check(Find(1) && Find(1)->weaponID == 0x123 && Find(1)->simulation.Style() == Redsand::Bow::TrailStyle::VanillaBow && Count(*Find(1), Kind::Ring) == 0,
			"recovered vanilla renderer lost its launch style or replayed the ring");
		actor.equippedWeapon = 0x123;
		auto custom = Sample(2, 3);
		custom.weaponID = 0x999;
		ProcessArrow(custom, 1.f);
		Check(Find(2) && Find(2)->weaponID == 0x999 && Find(2)->simulation.Style() == Redsand::Bow::TrailStyle::Default,
			"mod arrow inherited the actor's newly equipped vanilla bow style");
		auto bolt = Sample(3, 4);
		bolt.weaponID = 0x124;
		ProcessArrow(bolt, 1.f);
		Check(Find(3) && Find(3)->simulation.Style() == Redsand::Bow::TrailStyle::VanillaCrossbow && Count(*Find(3), Kind::Trail) > 0,
			"vanilla crossbow could not receive its own launch style");
	}

	void SaturatedQueueCoalescesAndPrioritizesBirths()
	{
		Reset();
		Redsand::Bow::SampleQueue<ArrowSample> queue(maxQueuedSamples);
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) {
			auto old = Sample(handle, handle);
			old.livingTime = 10.f;
			Check(queue.Push(old), "queue could not accept its initial bounded flight set");
		}
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) {
			auto update = Sample(handle, 3000 + handle);
			update.livingTime = 10.1f;
			update.to.y = 60.f;
			Check(queue.Push(update), "duplicate updates consumed separate queue capacity");
		}
		for (unsigned birth = 1; birth <= 64; ++birth) {
			Check(queue.Push(Sample(3000 + birth, 6000 + birth)), "old flights starved a fresh launch in the saturated queue");
		}
		Check(queue.Size() == maxQueuedSamples && queue.Merged() == maxQueuedSamples && queue.Dropped() == 64,
			"sample queue failed to remain bounded or account for coalescing and overload");
		std::vector<ArrowSample> drained;
		queue.Drain(drained);
		Check(queue.Size() == 0 && drained.size() == maxQueuedSamples, "draining did not release every queue slot");
		for (unsigned birth = 1; birth <= 64; ++birth) {
			Check(std::any_of(drained.begin(), drained.end(), [&](const auto& s) { return s.handle.value == 3000 + birth && s.livingTime == 0.f; }),
				"a fresh launch was lost before main-thread admission");
		}
		const auto existing = std::find_if(drained.begin(), drained.end(), [](const auto& s) { return s.handle.value == 100; });
		Check(existing != drained.end() && existing->sequence == 100 && existing->lastSequence == 3100 &&
			existing->latestLivingTime == 10.1f && existing->to.y == 60.f && std::abs(existing->delta - 2.f / 60.f) < 0.0001f,
			"coalescing lost launch order, latest flight position or elapsed sample time");

		queue.Clear();
		auto terminal = Sample(1, 30);
		terminal.livingTime = 0.2f;
		terminal.from.y = 30.f;
		terminal.to.y = 60.f;
		terminal.flying = false;
		queue.Push(terminal);
		queue.Push(Sample(1, 10));
		drained.clear();
		queue.Drain(drained);
		Check(drained.size() == 1 && drained[0].sequence == 10 && drained[0].lastSequence == 30 &&
			drained[0].from.y == 0.f && drained[0].to.y == 60.f && !drained[0].flying,
			"late worker changed a terminal flight into a new release or reversed its path");
	}

	void HandleReuseAndLateWorkersCannotPinOldShotState()
	{
		Reset();
		ProcessArrow(Sample(1, 10), 1.f);
		auto old = Sample(1, 20);
		old.livingTime = 2.f;
		old.flying = false;
		ProcessArrow(old, 1.f);
		Check(!Find(1)->emitting, "fixture did not finish the old handle incarnation");
		ProcessArrow(Sample(1, 30), 1.f);
		Check(Find(1) && Find(1)->emitting && Count(*Find(1), Kind::Ring) == 384 && shots.at({ 1 }).lastSampleSequence == 30,
			"a reused native handle inherited the old shot's stop or release tombstone");
		const auto count = Find(1)->simulation.Particles().size();
		ProcessArrow(old, 1.f);
		Check(Find(1)->emitting && Find(1)->simulation.Particles().size() == count && shots.at({ 1 }).lastSampleSequence == 30,
			"an older worker stopped or rewound a reused handle");

		Redsand::Bow::SampleQueue<ArrowSample> queue(2);
		queue.Push(old);
		queue.Push(Sample(1, 30));
		queue.Push(old);
		std::vector<ArrowSample> batch;
		queue.Drain(batch);
		Check(batch.size() == 1 && batch[0].sequence == 30 && batch[0].livingTime == 0.f && batch[0].flying,
			"the handoff queue merged two native handle incarnations");
	}

	void SustainedRapidFireSurvivesLedgerPressureAndRendererFailures()
	{
		Reset();
		constexpr unsigned frames = 600, shotsPerFrame = 8;
		std::vector<unsigned> bornFrame(1, 0);
		std::uint64_t sequence = 0;
		unsigned launched = 0, failedRenderers = 0;
		bool renderedAfterWarmup = false;
		for (unsigned frame = 0; frame < frames; ++frame) {
			for (auto& stream : streams) {
				stream->age += 1.f / 60.f;
				stream->simulation.Advance(1.f / 60.f, stream->tip);
			}

			for (const auto& [handle, state] : shots) {
				auto sample = Sample(handle.value, ++sequence);
				sample.livingTime = static_cast<float>(frame - bornFrame[handle.value]) / 60.f;
				ProcessArrow(sample, 1.f);
			}
			for (unsigned shot = 0; shot < shotsPerFrame; ++shot) {
				++launched;
				bornFrame.push_back(frame);
				ProcessArrow(Sample(launched, ++sequence), 1.f);
				Check(Find(launched) && Count(*Find(launched), Kind::Ring) == 384,
					"sustained rapid fire stopped admitting launches after ledger/stream saturation");
			}
			if (frame % 90 == 89) {
				RecoverableStreamLoss(*streams.front());
				streams.erase(streams.begin());
				++failedRenderers;
			}
			if (frame > 60) {

				const bool ready = std::any_of(streams.begin(), streams.end(), [](const auto& stream) {
					return stream->age - stream->rendererStartAge >= 0.6f;
				});
				Check(ready, "rapid lease turnover cancelled every renderer before asynchronous loading completed");
				renderedAfterWarmup = true;
			}
			Check(shots.size() <= maxQueuedSamples && streams.size() <= maxStreams, "continuous fire grew its bounded runtime state");
		}
		Check(launched == 4800 && renderedAfterWarmup && shots.size() == maxQueuedSamples &&
			spawnAttempts <= maxStreams + failedRenderers, "long burst churned clones or bypassed ledger pressure instead of recovering");
	}

	void CoalescedLongFlightStillCoversItsTrajectory()
	{
		Reset();
		Redsand::Bow::SampleQueue<ArrowSample> queue(32);
		for (unsigned step = 0; step < 12; ++step) {
			auto sample = Sample(1, step + 1);
			sample.from = { 0.f, step * 750.f, 0.f };
			sample.to = { 0.f, (step + 1) * 750.f, 0.f };
			sample.livingTime = step / 60.f;
			queue.Push(sample);
		}
		std::vector<ArrowSample> samples;
		queue.Drain(samples);
		Check(samples.size() == 1 && samples[0].continuous && samples[0].to.y == 9000.f,
			"connected native flight segments were lost during a long handoff batch");
		ProcessArrow(samples[0], 2.f, false, true);
		Check(Find(1) && Count(*Find(1), Kind::Trail) > 1000 && Count(*Find(1), Kind::Trail) <= 1024,
			"a legitimate fast/coalesced flight crossed the teleport guard or exceeded its birth budget");
		Check(Find(1)->simulation.Particles().back().position.y > 8990.f,
			"overload sampling emitted only at the start of the segment and left the current arrow invisible");
	}

	void OutstandingBirthsProtectOnlyTheirOwnRetiredRecords()
	{
		Reset();
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) { ProcessArrow(Sample(handle, handle), 1.f); }
		Check(birthReservations.Begin({ 1 }), "old birth reservation was refused before saturation");
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) {
			auto update = Sample(handle, 6000 + handle);
			update.livingTime = 1.f;
			ProcessArrow(update, 1.f);
		}
		ProcessArrow(Sample(3000, 9000), 1.f);
		Check(shots.contains({ 1 }) && !shots.contains({ 2 }) && Find(3000),
			"capacity reclamation discarded a tombstone while its original worker could still arrive");
		auto delayed = Sample(1, 3000);
		delayed.livingTime = 1.f / 60.f;
		CompleteBirth(delayed);
		Check(!birthReservations.Contains({ 1 }) && pendingSamples.HasBirth({ 1 }), "completion left a gap between reservation and queue protection");
		ProcessArrow(Sample(3001, 9001), 1.f);
		Check(shots.contains({ 1 }), "a queued birth lost its retirement protection");
		std::vector<ArrowSample> batch;
		{
			std::lock_guard lock(sampleLock);
			DrainArrowSamples(batch);
		}
		ProcessArrow(Sample(3002, 9002), 1.f);
		Check(shots.contains({ 1 }), "a drained birth lost protection before main-thread consumption");
		ProcessArrow(batch.front(), 1.f);
		Check(shots.at({ 1 }).lastSampleSequence == 6001 && !Find(1), "an old delayed birth replayed its release or renderer");
		ProcessArrow(Sample(3003, 9003), 1.f);
		Check(!shots.contains({ 1 }) && shots.size() == maxQueuedSamples, "consumed reservations kept tombstones pinned indefinitely");
		ProcessArrow(Sample(1, 9004), 1.f);
		Check(Find(1) && Count(*Find(1), Kind::Ring) == 384,
			"a genuine newer handle incarnation inherited the retired shot's rejection");
	}

	void TerminalYoungShotsCannotBlockEveryNewLaunch()
	{
		Reset();
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) {
			auto sample = Sample(handle, handle + 1000);
			sample.livingTime = 0.05f;
			sample.flying = false;
			ProcessArrow(sample, 1.f);
		}
		Check(shots.size() == maxQueuedSamples && streams.size() == maxStreams,
			"short-flight fixture did not fill its bounded release ledger");
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) {
			auto staleFlight = Sample(handle, handle + 5000);
			staleFlight.livingTime = 0.05f;
			ProcessArrow(staleFlight, 1.f);
			Check(shots.at({ handle }).flightEnded, "a later flying callback cleared a known terminal arrow state");
		}
		Check(birthReservations.Begin({ 1 }), "short-flight fixture could not reserve its late callback");
		ProcessArrow(Sample(3000, 9000), 1.f);
		Check(Find(3000) && shots.contains({ 1 }) && !shots.contains({ 2 }) && shots.size() == maxQueuedSamples,
			"terminal arrows younger than 0.25 seconds blocked all fresh rapid-fire effects");
		auto delayed = Sample(1, 500);
		delayed.livingTime = 0.05f;
		delayed.flying = false;
		CompleteBirth(delayed);
		ProcessArrow(Sample(3001, 9001), 1.f);
		Check(shots.contains({ 1 }) && Find(3001), "queued short-flight callback lost its tombstone protection");
		std::vector<ArrowSample> batch;
		{
			std::lock_guard lock(sampleLock);
			DrainArrowSamples(batch);
		}
		ProcessArrow(Sample(3002, 9002), 1.f);
		Check(shots.contains({ 1 }) && Find(3002), "drained short-flight callback lost its tombstone protection");
		ProcessArrow(batch.front(), 1.f);
		ProcessArrow(Sample(3003, 9003), 1.f);
		Check(!shots.contains({ 1 }) && Find(3003) && shots.size() == maxQueuedSamples && streams.size() == maxStreams,
			"consumed terminal callback pinned the rapid-fire ledger indefinitely");
	}

	void CoalescingPreservesTheNativeCurvedFlightAndBirthAges()
	{
		Reset();
		const Vec3 points[] = { {}, { 0.f, 90.f, 0.f }, { 90.f, 180.f, 0.f }, { 180.f, 180.f, 0.f } };
		Redsand::Bow::SampleQueue<ArrowSample> queue(32);
		Simulation reference(1);
		reference.SetStyle(Redsand::Bow::TrailStyle::VanillaBow);
		for (unsigned step = 0; step < 3; ++step) {
			auto sample = Sample(1, step + 1);
			sample.weaponID = 0x123;
			sample.from = points[step];
			sample.to = points[step + 1];
			sample.livingTime = step * 0.01f;
			sample.delta = 0.01f;
			queue.Push(sample);
			reference.Advance(sample.delta, {});
			reference.Trail(sample.from, sample.to, sample.delta, 1.f, true);
		}
		std::vector<ArrowSample> batch;
		queue.Drain(batch);
		Check(batch.size() == 1, "curved native flight unexpectedly split its release bookkeeping");
		ProcessArrow(batch.front(), 1.f, false, true);
		auto* stream = Find(1);
		Check(stream && stream->simulation.Particles().size() == reference.Particles().size(),
			"coalescing replaced the native curved flight with a shorter straight chord");
		for (std::size_t i = 0; i < reference.Particles().size(); ++i) {
			const auto& actual = stream->simulation.Particles()[i];
			const auto& expected = reference.Particles()[i];
			Check((actual.position - expected.position).Length() < 0.002f && std::abs(actual.age - expected.age) < 0.0001f &&
				std::abs(actual.Alpha() - expected.Alpha()) < 0.0001f,
				"coalescing changed particle scatter, birth time or native trajectory positions");
		}
	}

	void NativeFlightHistoryAndWholeBatchEmissionStayBounded()
	{
		Reset();
		Redsand::Bow::SampleQueue<ArrowSample> queue(4);
		constexpr unsigned segments = 256;
		for (unsigned step = 0; step < segments; ++step) {
			auto sample = Sample(1, step + 1);
			sample.from = { 0.f, step * 750.f, 0.f };
			sample.to = { 0.f, (step + 1) * 750.f, 0.f };
			sample.delta = 0.001f;
			sample.livingTime = step * 0.001f;
			queue.Push(sample);
		}
		std::vector<ArrowSample> batch;
		queue.Drain(batch);
		Check(batch.size() == 1 && batch[0].path.Size() <= Redsand::Bow::FlightPath::capacity && batch[0].path.Size() > 1,
			"overload grew an unbounded native path history or flattened it to a single replacement chord");
		ProcessArrow(batch[0], 2.f, false, true);
		Check(Find(1) && Count(*Find(1), Kind::Trail) > 1000 && Count(*Find(1), Kind::Trail) <= 1024 &&
			Find(1)->simulation.Particles().back().position.y > segments * 750.f - 200.f,
			"segmented overload exceeded the whole-batch birth budget or emitted only the old trajectory prefix");
	}

	void SilentCleanupWaitsForOutstandingBirthCallbacks()
	{
		Reset();
		ProcessArrow(Sample(1, 10), 1.f);
		Check(birthReservations.Begin({ 1 }), "fixture could not reserve its delayed birth callback");
		auto delayed = Sample(1, 20);
		delayed.livingTime = 0.1f;
		auto update = Sample(1, 30);
		update.livingTime = 1.f;
		ProcessArrow(update, 1.f);
		streams.clear();
		shots.at({ 1 }).silence = 5.f;
		PruneSilentShots();
		Check(shots.contains({ 1 }), "silent cleanup forgot a birth callback still executing natively");
		CompleteBirth(delayed);
		PruneSilentShots();
		Check(shots.contains({ 1 }), "silent cleanup forgot a queued old birth");
		ReplayPending();
		Check(streams.empty() && shots.at({ 1 }).lastSampleSequence == 30, "silent cleanup allowed an old birth to replay");
		PruneSilentShots();
		Check(shots.empty() && birthReservations.Count() == 0, "consumed birth callbacks retained silent tombstones forever");
		ProcessArrow(Sample(1, 40), 1.f);
		Check(Find(1), "new launches could not resume after silent tombstone cleanup");
	}

	void ProtectedChargesRecoverWithoutOverrunningTheRendererBudget()
	{
		Reset();
		for (unsigned i = 0; i < maxStreams; ++i) {
			auto* charge = CreateStream(&actor, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, i + 1);
			Check(charge, "fixture failed to fill the charging budget");
			charge->charging = true;
			charge->actor = 100 + i;
		}
		ProcessArrow(Sample(1, 100), 1.f);
		Check(!Find(1) && streams.size() == maxStreams && shots.at({ 1 }).releaseConsumed && !shots.at({ 1 }).streamCreated &&
			std::all_of(streams.begin(), streams.end(), [](const auto& stream) { return stream->charging && stream->emitting; }),
			"a full protected draw set was evicted, exceeded capacity or permanently consumed the spawn retry");
		streams.front()->Stop();
		auto retry = Sample(1, 110);
		retry.livingTime = 1.f;
		ProcessArrow(retry, 1.f);
		Check(Find(1) && streams.size() == maxStreams && Count(*Find(1), Kind::Ring) == 0 && Count(*Find(1), Kind::Trail) > 0,
			"a failed launch could not acquire a released charge slot without replaying its expired ring");

		Reset();
		for (unsigned i = 0; i < maxStreams; ++i) {
			auto* charge = CreateStream(&actor, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, i + 1);
			charge->charging = true;
			charge->actor = i ? 100 + i : actor.GetHandle();
			charge->chargeSequence = 50;
		}
		ProcessArrow(Sample(1, 100), 1.f);
		Check(Find(1) && streams.size() == maxStreams && Count(*Find(1), Kind::Ring) == 384 &&
			std::count_if(streams.begin(), streams.end(), [](const auto& stream) { return stream->charging && stream->emitting; }) == 31,
			"releasing an actor's own cloud failed to free its slot while all other draw clouds were protected");
	}

	void DelayedBirthSurvivesOtherShotRetirementAcrossBatches()
	{
		Reset();
		for (unsigned handle = 1; handle <= maxQueuedSamples; ++handle) { ProcessArrow(Sample(handle, handle), 1.f); }
		for (unsigned handle = 2; handle <= maxQueuedSamples; ++handle) {
			auto update = Sample(handle, 6000 + handle);
			update.livingTime = 1.f;
			ProcessArrow(update, 1.f);
		}
		Check(birthReservations.Begin({ 3000 }), "fixture could not reserve the late fresh launch");
		const auto delayedBirth = Sample(3000, 4000);
		auto old = Sample(1, 3000);
		old.livingTime = 1.f;
		pendingSamples.Push(old);
		old.sequence = 5000;
		old.livingTime = 1.1f;
		old.from.y = 30.f;
		old.to.y = 60.f;
		pendingSamples.Push(old);
		pendingSamples.Push(Sample(3001, 6000));
		pendingSamples.Push(Sample(3002, 6001));
		ReplayPending();
		Check(!shots.contains({ 1 }) && Find(3001) && Find(3002) && !Find(3000),
			"fixture did not retire the old coalesced flight while the fresh worker remained pending");
		CompleteBirth(delayedBirth);
		ReplayPending();
		Check(Find(3000) && Find(3001) && Find(3002) && Count(*Find(3000), Kind::Ring) == 384,
			"old-shot reclamation rejected a real fresh birth completing in the next worker batch");
		Check(shots.size() == maxQueuedSamples && streams.size() == maxStreams && birthReservations.Count() == 0,
			"cross-batch completion leaked reservations or exceeded the fixed runtime budgets");
	}

	void BirthReservationsStayBoundedAndReleaseEachCapture()
	{
		Redsand::Bow::BirthReservations<RE::ProjectileHandle> reservations(2);
		Check(reservations.Begin({ 1 }) && reservations.Begin({ 1 }) && !reservations.Begin({ 2 }) &&
			reservations.Count() == 2 && !reservations.Contains({ 2 }), "birth reservation saturation exceeded its hard bound");
		reservations.End({ 1 });
		Check(reservations.Contains({ 1 }) && reservations.Count() == 1, "one completion released another active capture on the same handle");
		reservations.End({ 1 });
		Check(!reservations.Contains({ 1 }) && reservations.Count() == 0 && reservations.Begin({ 2 }), "completed captures permanently exhausted the reservation budget");
		reservations.Clear();
		Check(reservations.Count() == 0 && !reservations.Contains({ 2 }), "save-generation cleanup retained active capture state");
	}

	void SameFrameBurstKeepsTheNewestRendererLeases()
	{
		Reset();
		for (unsigned handle = 1; handle <= 96; ++handle) { ProcessArrow(Sample(handle, handle), 1.f); }
		for (unsigned handle = 1; handle <= 96; ++handle) {
			Check((Find(handle) != nullptr) == (handle > 96 - maxStreams),
				"equal-age same-frame launches repeatedly reused one slot instead of retaining the newest renderer leases");
		}
		Check(streams.size() == maxStreams && spawnAttempts == maxStreams,
			"same-frame lease rotation restarted engine clones or exceeded the fixed renderer budget");
	}

	void QueueValidationAndContinuitySurviveOverload()
	{
		Reset();
		Redsand::Bow::SampleQueue<ArrowSample> queue(4);
		for (float delta : { -1.f, 0.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			auto bad = Sample(1, 1);
			bad.delta = delta;
			Check(!queue.Push(bad) && queue.Size() == 0, "an invalid native delta poisoned the handoff queue");
		}
		auto bad = Sample(1, 1);
		bad.latestLivingTime = std::numeric_limits<float>::quiet_NaN();
		Check(!queue.Push(bad), "nonfinite coalesced lifetime bypassed validation");
		bad = Sample(1, 2);
		bad.lastSequence = 1;
		Check(!queue.Push(bad), "an inverted capture interval bypassed validation");
		for (unsigned step = 0; step < 3; ++step) {
			auto part = Sample(1, step + 1);
			part.from.y = step == 2 ? 8000.f : step * 1500.f;
			part.to.y = step == 0 ? 1500.f : step == 1 ? 8000.f : 9500.f;
			part.continuous = step != 1;
			queue.Push(part);
		}
		std::vector<ArrowSample> batch;
		queue.Drain(batch);
		Check(batch.size() == 1 && !batch[0].continuous, "a later valid segment erased earlier teleport evidence");
		ProcessArrow(batch[0], 1.f, false, true);
		Check(Find(1) && Count(*Find(1), Kind::Trail) > 0 &&
			std::all_of(Find(1)->simulation.Particles().begin(), Find(1)->simulation.Particles().end(), [](const auto& particle) {
				return particle.position.y < 1505.f || particle.position.y > 7995.f;
			}), "a discontinuous batch bridged a teleport or discarded all legitimate native flight segments");
		queue.Clear();
		auto first = Sample(2, 10);
		first.to.y = 1500.f;
		auto last = Sample(2, 11);
		last.from.y = 2000.f;
		last.to.y = 3500.f;
		queue.Push(first);
		queue.Push(last);
		batch.clear();
		queue.Drain(batch);
		Check(batch.size() == 1 && !batch[0].continuous, "an unobserved gap was incorrectly treated as continuous native movement");
		ProcessArrow(batch[0], 1.f, false, true);
		Check(Find(2) && Count(*Find(2), Kind::Trail) > 0 &&
			std::all_of(Find(2)->simulation.Particles().begin(), Find(2)->simulation.Particles().end(), [](const auto& particle) {
				return particle.position.y < 1505.f || particle.position.y > 1995.f;
			}), "an unobserved short gap produced a false bridge between valid native segments");
		queue.Clear();
		first = Sample(3, 20);
		first.delta = std::numeric_limits<float>::max();
		last = Sample(3, 21);
		last.from.y = 30.f;
		last.to.y = 60.f;
		last.delta = std::numeric_limits<float>::max();
		last.flying = false;
		queue.Push(first);
		queue.Push(last);
		batch.clear();
		queue.Drain(batch);
		Check(batch.size() == 1 && std::isfinite(batch[0].delta) && batch[0].delta == std::numeric_limits<float>::max() && !batch[0].flying && batch[0].to.y == 60.f,
			"finite deltas overflowed the batch or lost its terminal native endpoint");
		ProcessArrow(batch[0], 1.f, false, true);
		Check(Find(3) && !Find(3)->emitting && Count(*Find(3), Kind::Trail) > 0 &&
			std::all_of(Find(3)->simulation.Particles().begin(), Find(3)->simulation.Particles().end(), [](const auto& particle) { return particle.position.Finite(); }),
			"the finite saturated batch lost its endpoint or produced invalid particle positions");
	}

	void PlayerAndNPCScopesFilterNewAndExistingEffects()
	{
		Reset();
		RE::Actor npc;
		npc.handle = 2;
		npc.player = false;
		auto& configuration = RedsandSettings::current;
		for (unsigned mask = 0; mask < 4; ++mask) {
			configuration.bowPlayer = (mask & 1) != 0;
			configuration.bowNPC = (mask & 2) != 0;
			Check(Allowed(&actor) == configuration.bowPlayer && Allowed(&npc) == configuration.bowNPC,
				"the production scope predicate inverted or conflated player and NPC settings");
		}
		configuration.bowEnabled = false;
		Check(!Allowed(&actor) && !Allowed(&npc) && !Allowed(nullptr), "disabled bow effects admitted an actor");
		configuration.bowEnabled = true;
		actor.loaded = false;
		Check(!Allowed(&actor), "an unloaded actor passed the production creation predicate");
		actor.loaded = true;
		actor.dead = true;
		Check(!Allowed(&actor), "a dead actor passed the production creation predicate");
		actor.dead = false;
		auto npcSample = Sample(2, 2);
		npcSample.shooter = { &npc };
		ProcessArrow(Sample(1, 1), 1.f);
		ProcessArrow(npcSample, 1.f);
		auto* playerCharge = CreateStream(&actor, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, 10);
		auto* npcCharge = CreateStream(&npc, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, 20);
		Check(playerCharge && npcCharge && Find(1) && Find(2), "scope fixture did not create both actors' effects");
		playerCharge->charging = npcCharge->charging = true;
		const auto resolveActor = [&](const Stream& stream) -> RE::Actor* {
			return stream.actor == actor.handle ? &actor : stream.actor == npc.handle ? &npc : nullptr;
		};
		configuration.bowNPC = false;
		ApplyStreamConfiguration(configuration, true, resolveActor);
		Check(streams.size() == 2 && Find(1) && !Find(2) && Stream::retired == 2 && !shots.at({ 2 }).rendererResident,
			"switching to player-only retained an NPC charge or projectile effect");
		Check(!CreateStream(&npc, RE::TESForm::LookupByID<RE::TESObjectCELL>(1), {}, 30), "a direct stream creation bypassed player-only scope");
		npcSample.handle = { 20 };
		npcSample.sequence = 3;
		ProcessArrow(npcSample, 1.f);
		ProcessArrow(Sample(10, 4), 1.f);
		Check(!Find(20) && !shots.contains({ 20 }) && Find(10), "player-only scope admitted an NPC release or rejected the player");
		configuration.bowPlayer = false;
		configuration.bowNPC = true;
		ApplyStreamConfiguration(configuration, true, resolveActor);
		Check(streams.empty() && Stream::retired == 5, "switching to NPC-only retained a player charge or trail");
		npcSample.handle = { 2 };
		npcSample.sequence = 6;
		npcSample.livingTime = 1.f;
		ProcessArrow(npcSample, 1.f);
		auto playerUpdate = Sample(10, 7);
		playerUpdate.livingTime = 1.f;
		ProcessArrow(playerUpdate, 1.f);
		Check(Find(2) && Count(*Find(2), Kind::Ring) == 0 && Count(*Find(2), Kind::Trail) > 0 && !Find(10),
			"NPC-only scope lost the NPC's existing flight, replayed its ring or restored a player trail");
		configuration.bowNPC = false;
		ApplyStreamConfiguration(configuration, true, resolveActor);
		Check(streams.empty(), "disabling both scopes left a visible bow stream");
		Check(RedsandLog::scopeReports == 0, "disabled diagnostics performed detailed scope logging");
		configuration.bowPlayer = configuration.bowNPC = true;
		configuration.diagnostics = true;
		Check(Allowed(&actor) && RedsandLog::scopeReports == 1, "late-enabled diagnostics did not report the active player scope");
		Allowed(&actor);
		Check(RedsandLog::scopeReports == 1, "scope diagnostics repeated on every observation");
		configuration.bowPlayer = false;
		Check(!Allowed(&actor) && RedsandLog::scopeReports == 2, "scope changes did not report the new rejected actor decision");
	}


    void IndependentCategorySurvivesDisabledMainBowSettings()
    {
        Reset();
        ProcessArrow(Sample(1, 1), 1.f);
        auto* stream = Find(1); Check(stream != nullptr, "category bow fixture missing");
        stream->enchantmentKey = "fire";
        auto config = RedsandSettings::Get();
        config.bowEnabled = config.bowPlayer = config.bowNPC = false;
        config.bowGathering = config.bowRings = config.bowTrails = false;
        RedsandSettings::EnchantmentAppearance profile; profile.enabled = true; profile.bowRings = false; profile.bowDensity = .25f;
        config.enchantments.Set("fire", profile);
        ApplyStreamConfiguration(config, true, [](const Stream&) { return &actor; });
        Check(Find(1) != nullptr && Count(*Find(1), Kind::Ring) == 0,
            "disabled main page blocked category bow stream or category ring disable was ignored");
        RedsandSettings::current = config;
        auto later = Sample(1, 2); later.livingTime = .3f;
        ProcessArrow(later, 0.f, false, false);
        Check(Find(1) && Count(*Find(1), Kind::Trail) > 0 && Find(1)->enchantmentKey == "fire",
            "in-flight category was replaced by main bow values on the next sample");
        profile.bowEnabled = false; config.bowEnabled = config.bowPlayer = config.bowTrails = true;
        config.enchantments.Set("fire", profile);
        ApplyStreamConfiguration(config, true, [](const Stream&) { return &actor; });
        Check(Find(1) == nullptr, "enabled main page resurrected a disabled category bow stream");
    }

}

int main() try
{
        IndependentCategorySurvivesDisabledMainBowSettings();
	FullBudgetKeepsAcceptingShotsAndDoesNotReplayEvictedArrows();
	FailedSpawnRetriesWithoutCancellingTheNextDraw();
	OlderWorkerSampleCannotCancelANewerDraw();
	FirstReleaseCreatesItsChargeFence();
	SamplingGapsAndLongFlightsRemainRecoverable();
	RendererLossRebuildsOnlyTheRemainingTrail();
	LiveFeaturesDoNotReplayLaunches();
	LaunchWeaponSurvivesEquipmentChangesAndRecovery();
	SaturatedQueueCoalescesAndPrioritizesBirths();
	HandleReuseAndLateWorkersCannotPinOldShotState();
	SustainedRapidFireSurvivesLedgerPressureAndRendererFailures();
	CoalescedLongFlightStillCoversItsTrajectory();
	TerminalYoungShotsCannotBlockEveryNewLaunch();
	CoalescingPreservesTheNativeCurvedFlightAndBirthAges();
	NativeFlightHistoryAndWholeBatchEmissionStayBounded();
	OutstandingBirthsProtectOnlyTheirOwnRetiredRecords();
	SilentCleanupWaitsForOutstandingBirthCallbacks();
	ProtectedChargesRecoverWithoutOverrunningTheRendererBudget();
	DelayedBirthSurvivesOtherShotRetirementAcrossBatches();
	BirthReservationsStayBoundedAndReleaseEachCapture();
	SameFrameBurstKeepsTheNewestRendererLeases();
	QueueValidationAndContinuitySurviveOverload();
	PlayerAndNPCScopesFilterNewAndExistingEffects();
	std::cout << "PASS: production arrow budget, ordered release, sampling gaps, renderer recovery and live features\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
