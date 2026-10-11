#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

#include "../source/RedsandBowSimulation.h"
#include "../source/RedsandBowStreamBudget.h"

using namespace std::literals;

void check(bool condition, const char* message)
{
	if (!condition) { throw std::runtime_error(message); }
}

template <class T> struct Pointer
{
	T* value = nullptr;
	T* get() const { return value; }
	T* operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }
	void reset(T* next = nullptr) { value = next; }
};

namespace RE
{
	struct BSGeometry;
	struct NiColorA { float red = 1.f, green = 1.f, blue = 1.f, alpha = 1.f; };
	struct NiAVObject
	{
		virtual ~NiAVObject() = default;
		std::vector<BSGeometry*> geometries;
		NiAVObject* parent = nullptr;
		bool controllersActive = true;
		bool culled = false;
		void SetAppCulled(bool value) { culled = value; }
	};
	struct BSEffectShaderMaterial
	{
		virtual ~BSEffectShaderMaterial() = default;
		bool allocationFails = false;
		NiColorA baseColor;
		float baseColorScale = 0.f;
		struct Texture { std::string name; };
		Pointer<Texture> sourceTexture;
		std::string sourceTexturePath = "textures/renderer-fixture.dds";
		BSEffectShaderMaterial* Create()
		{
			if (allocationFails) { return nullptr; }
			return new (std::malloc(sizeof(BSEffectShaderMaterial))) BSEffectShaderMaterial;
		}
		void CopyMembers(BSEffectShaderMaterial* other)
		{
			baseColor = other->baseColor;
			baseColorScale = other->baseColorScale;
			sourceTexture = other->sourceTexture;
			sourceTexturePath = other->sourceTexturePath;
		}
	};
	struct BSEffectShaderProperty
	{
		virtual ~BSEffectShaderProperty() = default;
		BSEffectShaderMaterial* material = nullptr;
		unsigned assignments = 0;
		NiColorA assignedColor;
		float assignedScale = 0.f;
		void SetMaterial(BSEffectShaderMaterial* copy, bool)
		{
			++assignments;
			assignedColor = copy->baseColor;
			assignedScale = copy->baseColorScale;
		}
	};
	inline void free(void* pointer) { std::free(pointer); }
	struct BSGeometry : NiAVObject
	{
		std::string_view name;
		struct Runtime { Pointer<BSEffectShaderProperty> shaderProperty; } runtime;
		Runtime& GetGeometryRuntimeData() { return runtime; }
	};
	struct Modifier
	{
		bool active = true;
		void SetActive(bool value) { active = value; }
	};
	struct ParticleData
	{
		struct Runtime
		{
			void* positions = nullptr;
			void* color = nullptr;
			void* radii = nullptr;
			void* sizes = nullptr;
			unsigned maxNumVertices = 0;
			unsigned numVertices = 4;
		} runtime;
		Runtime& GetParticlesRuntimeData() { return runtime; }
	};
	struct NiParticleSystem : BSGeometry
	{
		struct Transform { float scale = 1.f; } world;
		struct ParticlesRuntime { Pointer<ParticleData> particleData; } particles;
		struct SystemRuntime
		{
			std::vector<Pointer<Modifier>> modifierList;
			bool isWorldspace = true, dynamicBounds = true, resetSystem = true;
		} system;
		ParticlesRuntime& GetParticlesRuntimeData() { return particles; }
		SystemRuntime& GetParticleSystemRuntimeData() { return system; }
	};
	namespace BSVisit
	{
		enum class BSVisitControl { kContinue };
		template <class Visitor> void TraverseScenegraphGeometries(NiAVObject* root, Visitor visitor)
		{
			for (auto* geometry : root->geometries) { visitor(geometry); }
		}
	}
}

template <class T, class U> T netimmerse_cast(U* value) { return dynamic_cast<T>(value); }
template <class T, class U> T skyrim_cast(U* value) { return dynamic_cast<T>(value); }

struct Effect
{
	Pointer<RE::NiAVObject> particleObject;
	float lifetime = 7.f, age = 0.f;
};
struct Stream
{
	Pointer<Effect> effect;
	Pointer<RE::NiParticleSystem> system;
	RE::NiAVObject* rendererRoot = nullptr;
	RE::NiColorA color{ 0.8f, 0.2f, 0.1f, 1.f };
	Redsand::Bow::Simulation simulation;
	std::string meshPath = "renderer-fixture.nif";
	float age = 0.f, rendererStartAge = 0.f, rendererBrightness = -1.f;
	float chargeTime = 0.f, silence = 0.f;
	std::uint64_t chargeSequence = 0;
	unsigned projectile = 0;
	bool charging = false, emitting = true, seen = false;
	bool rendererWasAttached = false;
	bool rendererResourcesPending = false;
};
constexpr char particleMesh[] = "renderer-fixture.nif";
unsigned warnings = 0;
unsigned materialReports = 0;
namespace RedsandLog
{
	template <class... Args> void warn(Args&&...) { ++warnings; }
	template <class... Args> void info(Args&&...) { ++materialReports; }
}

void DisableControllers(RE::NiAVObject* root)
{
	root->controllersActive = false;
	for (auto* geometry : root->geometries) { geometry->controllersActive = false; }
}

#include "../source/BowRenderer.inc"

struct Fixture
{
	RE::NiAVObject root;
	RE::BSGeometry nativeGeometry;
	RE::NiParticleSystem particles;
	RE::ParticleData data;
	RE::BSEffectShaderMaterial material;
	RE::BSEffectShaderProperty shader;
	RE::Modifier modifier;
	Effect effect;
	Stream stream;
	int storage = 0;

	Fixture()
	{
		nativeGeometry.name = "native emitter";
		particles.name = "RedsandParticles_Drain_v1";
		root.geometries = { &nativeGeometry, &particles };
		nativeGeometry.parent = particles.parent = &root;
		particles.particles.particleData.reset(&data);
		particles.runtime.shaderProperty.reset(&shader);
		particles.system.modifierList.push_back({ &modifier });
		shader.material = &material;
		data.runtime = { &storage, &storage, &storage, &storage, 8192, 4 };
		effect.particleObject.reset(&root);
		stream.effect.reset(&effect);
	}

	void Untouched() const
	{
		check(root.controllersActive && particles.controllersActive && nativeGeometry.controllersActive,
			"pending initialization disabled native controllers");
		check(!particles.culled && !nativeGeometry.culled, "pending initialization culled graph geometry");
		check(modifier.active && particles.system.resetSystem && data.runtime.numVertices == 4,
			"pending initialization changed native particle state");
		check(shader.assignments == 0 && !stream.system, "pending initialization committed a partial renderer");
	}

	void Ready()
	{
		check(PrepareRenderer(stream) == RendererStatus::Ready, "renderer did not recover when its resources became ready");
		check(stream.system.get() == &particles, "renderer selected the wrong particle system");
		check(!root.controllersActive && !modifier.active, "ready renderer retained native animation");
		check(nativeGeometry.culled && !particles.culled, "ready renderer failed to isolate its geometry");
		check(shader.assignments == 1 && shader.assignedColor.red == stream.color.red,
			"ready renderer failed to copy its instance color exactly once");
		check(data.runtime.numVertices == 0 && !particles.system.resetSystem,
			"ready renderer did not initialize particle storage");
	}
};

void delayedResources()
{
	Fixture root;
	root.effect.particleObject.reset();
	root.effect.age = 4.f;
	check(PrepareRenderer(root.stream) == RendererStatus::Pending, "delayed root failed immediately");
	check(root.effect.lifetime >= 11.f, "pending effect lifetime was not extended");
	root.Untouched();
	root.effect.particleObject.reset(&root.root);
	root.Ready();

	Fixture vertices;
	vertices.data.runtime.positions = nullptr;
	for (int frame = 0; frame < 90; ++frame) {
		vertices.stream.age = frame / 60.f;
		check(PrepareRenderer(vertices.stream) == RendererStatus::Pending, "delayed vertex buffer poisoned the renderer");
		vertices.Untouched();
	}
	vertices.data.runtime.positions = &vertices.storage;
	vertices.Ready();

	Fixture material;
	material.shader.material = nullptr;
	check(PrepareRenderer(material.stream) == RendererStatus::Pending, "delayed material failed immediately");
	material.Untouched();
	material.shader.material = &material.material;
	material.Ready();

	Fixture allocation;
	allocation.material.allocationFails = true;
	check(PrepareRenderer(allocation.stream) == RendererStatus::Pending, "temporary material allocation failure was permanent");
	allocation.Untouched();
	allocation.material.allocationFails = false;
	allocation.Ready();
	check(warnings == 0, "normal asynchronous loading produced failure warnings");
}

void timeoutIsolation()
{
	Fixture existing;
	existing.Ready();
	Fixture timeout;
	timeout.data.runtime.sizes = nullptr;
	timeout.stream.age = 1.999f;
	check(PrepareRenderer(timeout.stream) == RendererStatus::Pending, "renderer timed out before its loading budget");
	timeout.stream.age = 2.f;
	const auto before = warnings;
	check(PrepareRenderer(timeout.stream) == RendererStatus::Failed, "renderer did not retire at its loading deadline");
	check(warnings == before + 1, "renderer timeout did not identify the failed instance");
	timeout.Untouched();
	existing.stream.age = 12.f;
	check(PrepareRenderer(existing.stream) == RendererStatus::Ready, "another instance's timeout stopped a ready renderer");
	check(existing.shader.assignments == 1, "ready renderer was unnecessarily reinitialized");
	existing.effect.particleObject.reset();
	check(PrepareRenderer(existing.stream) == RendererStatus::Pending && !existing.stream.system,
		"unloaded effect exposed its old renderer or could not retry a temporary detach");
	existing.stream.age = 14.f;
	check(PrepareRenderer(existing.stream) == RendererStatus::Failed, "permanently unloaded effect was retained indefinitely");
	Fixture later;
	later.Ready();
	Fixture deadline;
	deadline.stream.age = 2.f;
	deadline.Ready();
	Stream missingEffect;
	check(PrepareRenderer(missingEffect) == RendererStatus::Failed, "missing effect was dereferenced or kept alive");
}

void rapidBurstRecovery()
{
	std::vector<std::unique_ptr<Fixture>> burst;
	for (int shot = 0; shot < 96; ++shot) {
		auto instance = std::make_unique<Fixture>();
		instance->data.runtime.color = nullptr;
		check(PrepareRenderer(instance->stream) == RendererStatus::Pending, "rapid burst treated pending data as a global failure");
		instance->Untouched();
		burst.push_back(std::move(instance));
	}
	for (std::size_t shot = 0; shot < burst.size(); ++shot) {
		auto& instance = *burst[shot];
		instance.stream.age = 2.f;
		if (shot % 3 == 0) {
			check(PrepareRenderer(instance.stream) == RendererStatus::Failed, "exhausted burst instance was not retired");
			instance.Untouched();
		} else {
			instance.data.runtime.color = &instance.storage;
			instance.Ready();
		}
	}
	Fixture nextShot;
	nextShot.Ready();
}

void rootReplacementAndLiveBrightness()
{
	Fixture original;
	RE::NiAVObject scene;
	original.root.parent = &scene;
	original.Ready();
	const float scale = original.shader.assignedScale;
	check(PrepareRenderer(original.stream, 2.f) == RendererStatus::Ready && original.shader.assignments == 2 &&
		original.shader.assignedScale == scale * 2.f, "live brightness did not update an already visible stream");
	check(PrepareRenderer(original.stream, 2.f) == RendererStatus::Ready && original.shader.assignments == 2,
		"unchanged brightness cloned a material every frame");
	check(PrepareRenderer(original.stream, 0.f) == RendererStatus::Ready && original.shader.assignedScale == 0.f,
		"zero brightness failed to darken an existing stream");
	original.material.allocationFails = true;
	check(PrepareRenderer(original.stream, 0.5f) == RendererStatus::Ready && original.stream.rendererBrightness == 0.f,
		"temporary brightness allocation failure destroyed a working renderer");
	original.material.allocationFails = false;
	check(PrepareRenderer(original.stream, 0.5f) == RendererStatus::Ready && original.shader.assignedScale == scale * 0.5f,
		"live brightness did not retry a temporary material failure");

	Fixture replacement;
	original.stream.age = 30.f;
	original.effect.particleObject.reset(&replacement.root);
	replacement.data.runtime.positions = nullptr;
	check(PrepareRenderer(original.stream) == RendererStatus::Pending && !original.stream.system,
		"replacement root retained the detached renderer or inherited an expired loading deadline");
	original.stream.age = 31.f;
	replacement.data.runtime.positions = &replacement.storage;
	check(PrepareRenderer(original.stream) == RendererStatus::Ready && original.stream.system.get() == &replacement.particles &&
		original.stream.rendererRoot == &replacement.root, "republished root did not bind its own visible particle system");
	check(replacement.shader.assignments == 1 && !replacement.root.controllersActive,
		"replacement renderer was not configured as an independent instance");

	original.effect.particleObject.reset();
	original.stream.age = 40.f;
	check(PrepareRenderer(original.stream) == RendererStatus::Pending && !original.stream.system,
		"temporary root detach kept an invisible renderer");
	original.stream.age = 40.5f;
	original.effect.particleObject.reset(&original.root);
	check(PrepareRenderer(original.stream) == RendererStatus::Ready && original.stream.system.get() == &original.particles,
		"original root could not recover after temporary detachment");
	original.root.parent = nullptr;
	check(PrepareRenderer(original.stream) == RendererStatus::Failed, "detached temp effect was kept alive while writing invisible particles");
	Fixture expired;
	expired.Ready();
	expired.effect.age = expired.effect.lifetime + 0.1f;
	const auto lifetime = expired.effect.lifetime;
	check(PrepareRenderer(expired.stream) == RendererStatus::Failed && expired.effect.lifetime == lifetime,
		"extending an expired temp effect resurrected a pointer absent from the engine manager");
}

void recycledRendererKeepsItsResourcesAndOriginalDeadline()
{
	Fixture pending;
	pending.data.runtime.sizes = nullptr;
	check(PrepareRenderer(pending.stream) == RendererStatus::Pending, "fixture did not delay its clone resources");
	for (unsigned lease = 0; lease < 199; ++lease) {
		pending.stream.age += 0.01f;
		Redsand::Bow::ResetStreamLease(pending.stream, lease + 1);
		check(PrepareRenderer(pending.stream) == RendererStatus::Pending, "reused pending renderer expired before its original loading deadline");
	}
	pending.stream.age += 0.02f;
	Redsand::Bow::ResetStreamLease(pending.stream, 200);
	check(PrepareRenderer(pending.stream) == RendererStatus::Failed,
		"high-rate reuse indefinitely postponed the failed renderer's initialization deadline");
	Fixture ready;
	ready.Ready();
	auto* system = ready.stream.system.get();
	ready.stream.age = 1.f;
	ready.stream.simulation.Ring({}, { 0.f, 1.f, 0.f }, 1.f, 1.f);
	Redsand::Bow::ResetStreamLease(ready.stream, 2);
	check(ready.stream.simulation.Empty() && ready.stream.system.get() == system && ready.stream.effect.get() == &ready.effect,
		"recycling destroyed the initialized engine renderer or retained the old arrow's particles");
	check(PrepareRenderer(ready.stream) == RendererStatus::Ready && ready.shader.assignments == 2,
		"reused renderer could not update the new launch's material without respawning");

	check(materialReports == 0, "disabled diagnostics printed or consumed a profile report");
	const auto reports = materialReports;
	for (auto style : { Redsand::Bow::TrailStyle::Default, Redsand::Bow::TrailStyle::VanillaBow, Redsand::Bow::TrailStyle::VanillaCrossbow }) {
		Fixture profile;
		profile.stream.simulation.SetStyle(style);
		profile.Ready();
		const auto assignments = profile.shader.assignments;
		const auto before = materialReports;
		check(PrepareRenderer(profile.stream, 1.f, true) == RendererStatus::Ready && materialReports == before + 1 &&
			profile.shader.assignments == assignments, "enabling diagnostics after initialization did not report the existing material without changing it");
		PrepareRenderer(profile.stream, 2.f, true);
		PrepareRenderer(profile.stream, 3.f, true);
		PrepareRenderer(profile.stream, 3.f, false);
	}
	check(materialReports == reports + 3, "profile material diagnostics were absent or repeated on every brightness change");
}

void transientNativeResourcesWaitWithoutRestartingTheirClone()
{
	Fixture warming;
	warming.particles.world.scale = 0.f;
	check(PrepareRenderer(warming.stream) == RendererStatus::Pending && warming.stream.system.get() == &warming.particles,
		"a zero-scale asynchronously cloned renderer was treated as ready and immediately discarded");
	warming.stream.age = 0.2f;
	warming.particles.world.scale = 1.f;
	check(PrepareRenderer(warming.stream) == RendererStatus::Ready && warming.shader.assignments == 1,
		"a warming renderer could not recover its transform without restarting its clone");
	warming.stream.age = 30.f;
	warming.data.runtime.positions = nullptr;
	check(PrepareRenderer(warming.stream) == RendererStatus::Pending && warming.stream.rendererStartAge == 30.f,
		"a temporary native buffer reset inherited an old deadline or exposed invalid particle storage");
	warming.stream.age = 30.5f;
	warming.data.runtime.positions = &warming.storage;
	check(PrepareRenderer(warming.stream) == RendererStatus::Ready && warming.shader.assignments == 1,
		"a transient buffer reset destroyed a working renderer instead of preserving its native resources");
	warming.particles.world.scale = 0.f;
	warming.stream.age = 40.f;
	check(PrepareRenderer(warming.stream) == RendererStatus::Pending, "a later transform reset did not get its own bounded recovery window");
	for (unsigned lease = 0; lease < 199; ++lease) {
		warming.stream.age += 0.01f;
		Redsand::Bow::ResetStreamLease(warming.stream, lease + 1);
		check(PrepareRenderer(warming.stream) == RendererStatus::Pending,
			"rapid lease turnover restarted or prematurely exhausted a transient renderer's loading window");
	}
	warming.stream.age += 0.02f;
	Redsand::Bow::ResetStreamLease(warming.stream, 200);
	check(PrepareRenderer(warming.stream) == RendererStatus::Failed,
		"rapid lease turnover kept a renderer with invalid native resources alive indefinitely");
	Fixture separate;
	separate.Ready();
}

void diagnosticRateAndDisabledWork()
{
	struct Clock
	{
		using time_point = std::chrono::steady_clock::time_point;
		static time_point now()
		{
			++calls();
			return current();
		}
		static unsigned& calls() { static unsigned value = 0; return value; }
		static time_point& current() { static time_point value = time_point{} + std::chrono::seconds(10); return value; }
	};
	Clock::time_point first, second;
	for (unsigned i = 0; i < 10000; ++i) {
		check(!RendererDiagnosticDue<Clock>(false, first), "disabled renderer diagnostics admitted a snapshot");
	}
	check(Clock::calls() == 0 && first == Clock::time_point{}, "disabled renderer diagnostics queried time or consumed the next snapshot");
	check(RendererDiagnosticDue<Clock>(true, first) && RendererDiagnosticDue<Clock>(true, second),
		"one renderer consumed another renderer's diagnostic deadline");
	for (unsigned frame = 1; frame < 100; ++frame) {
		Clock::current() += std::chrono::milliseconds(10);
		check(!RendererDiagnosticDue<Clock>(true, first), "one renderer produced multiple snapshots in a second");
	}
	Clock::current() += std::chrono::milliseconds(10);
	check(RendererDiagnosticDue<Clock>(true, first), "renderer did not record its next native frame after one second");
	const auto before = Clock::calls();
	check(!RendererDiagnosticDue<Clock>(false, second) && Clock::calls() == before,
		"turning diagnostics off still consulted the clock");
}

void hiddenEffectRootRecoversOnlyForItsLiveParticles()
{
	Fixture live;
	RE::NiAVObject scene, actor, globalScene;
	live.root.parent = &scene;
	scene.parent = &actor;
	actor.parent = &globalScene;
	scene.SetAppCulled(true);
	actor.SetAppCulled(true);
	globalScene.SetAppCulled(true);
	live.root.SetAppCulled(true);
	live.Ready();
	check(live.root.culled, "renderer preparation changed root visibility before particles were submitted");
	check(SubmitRendererVisibility(live.stream, 650), "live owned renderer rejected its visibility submission");
	check(!live.root.culled && !live.particles.culled,
		"live submitted particles left their owned effect root hidden");
	check(live.nativeGeometry.culled && scene.culled && actor.culled && globalScene.culled,
		"root recovery revealed unrelated geometry or external scene ancestors");
	for (unsigned frame = 0; frame < 120; ++frame) {
		live.root.SetAppCulled(true);
		check(SubmitRendererVisibility(live.stream, 650), "later native root hiding invalidated a live ownership path");
		check(!live.root.culled, "a later native root hide permanently suppressed a live renderer");
	}
	live.stream.emitting = false;
	live.root.SetAppCulled(true);
	check(SubmitRendererVisibility(live.stream, 650) && !live.root.culled && !live.particles.culled,
		"a stopped emitter hid its remaining fading particles");
	live.root.SetAppCulled(true);
	check(SubmitRendererVisibility(live.stream, 0), "empty owned renderer rejected its visibility submission");
	check(live.root.culled && live.particles.culled, "empty submission forcibly revealed its effect root");
	live.root.SetAppCulled(false);
	check(SubmitRendererVisibility(live.stream, 0), "empty visible root failed its ownership path");
	check(!live.root.culled && live.particles.culled, "empty submission introduced a sticky hidden effect root");
	RE::NiAVObject intermediate;
	intermediate.parent = &live.root;
	intermediate.SetAppCulled(true);
	live.particles.parent = &intermediate;
	live.root.SetAppCulled(true);
	check(SubmitRendererVisibility(live.stream, 650), "valid intermediate ancestry failed its ownership path");
	check(!live.root.culled && !live.particles.culled && intermediate.culled,
		"owned root recovery changed an intermediate ancestor's visibility");
	live.root.SetAppCulled(true);
	live.particles.SetAppCulled(true);
	live.stream.rendererRoot = &scene;
	check(!SubmitRendererVisibility(live.stream, 650), "mismatched root was reported as a successful submission");
	check(live.root.culled && live.particles.culled, "mismatched renderer root revealed stale resources");
	live.stream.rendererRoot = &live.root;
	live.particles.parent = &scene;
	check(!SubmitRendererVisibility(live.stream, 650), "detached ancestry was reported as a successful submission");
	check(live.root.culled && live.particles.culled, "a detached particle system revealed an unrelated effect root");
	intermediate.parent = &intermediate;
	live.particles.parent = &intermediate;
	check(!SubmitRendererVisibility(live.stream, 650), "cyclic ancestry was reported as a successful submission");
	check(live.root.culled && live.particles.culled, "a cyclic ancestry escaped its bounded ownership check");
	live.particles.parent = &live.root;
	live.effect.particleObject.reset(&live.particles);
	live.stream.rendererRoot = &live.particles;
	check(!SubmitRendererVisibility(live.stream, 650), "unsupported system-as-root layout reached external bounds ancestors");
	check(live.root.culled && live.particles.culled, "unsupported system-as-root layout changed visibility");
	live.effect.particleObject.reset(&live.root);
	live.stream.rendererRoot = &live.root;
	live.effect.age = live.effect.lifetime + 1.f;
	const auto lifetime = live.effect.lifetime;
	check(PrepareRenderer(live.stream) == RendererStatus::Failed, "retired effect passed the live renderer gate");
	check(!SubmitRendererVisibility(live.stream, 650), "expired effect was reported as a successful submission");
	check(live.root.culled && live.particles.culled && live.effect.lifetime == lifetime,
		"visibility recovery resurrected an expired or retired effect");
	Fixture detached;
	detached.root.parent = &scene;
	detached.Ready();
	detached.root.parent = nullptr;
	detached.root.SetAppCulled(true);
	const auto ready = PrepareRenderer(detached.stream);
	if (ready == RendererStatus::Ready) { SubmitRendererVisibility(detached.stream, 650); }
	check(ready == RendererStatus::Failed && detached.root.culled,
		"detached renderer bypassed the production preparation gate");
	Fixture pending;
	pending.root.SetAppCulled(true);
	pending.data.runtime.positions = nullptr;
	const auto waiting = PrepareRenderer(pending.stream);
	if (waiting == RendererStatus::Ready) { SubmitRendererVisibility(pending.stream, 650); }
	check(waiting == RendererStatus::Pending && pending.root.culled,
		"pending renderer revealed a root before submitting valid particle storage");
}

int main() try
{
	delayedResources();
	timeoutIsolation();
	rapidBurstRecovery();
	rootReplacementAndLiveBrightness();
	recycledRendererKeepsItsResourcesAndOriginalDeadline();
	transientNativeResourcesWaitWithoutRestartingTheirClone();
	diagnosticRateAndDisabledWork();
	hiddenEffectRootRecoversOnlyForItsLiveParticles();
	std::cout << "PASS: production renderer loading, isolated failure, scene graph replacement and live brightness\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
