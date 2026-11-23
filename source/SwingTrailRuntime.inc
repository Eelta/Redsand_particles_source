#include <bit>
#include <cstring>

namespace
{
	constexpr auto redsandSwingMesh = "RedsandSwingTrail/Arc.nif";

	RedsandSwingTrail::Vec3 SwingVector(const RE::NiPoint3& point)
	{
		return {point.x, point.y, point.z};
	}

	void GatherRedsandBladeTip(RE::NiAVObject& object, RE::NiTransform parent, unsigned depth,
		RedsandSwingTrail::BladeTip& vertices, RedsandSwingTrail::BladeTip& bounds, unsigned& remaining)
	{
		if (!depth || !remaining || object.GetAppCulled() || !RedsandSwingTrail::IsBladePart(object.name.c_str())) { return; }
		const auto local = parent * object.local;
		auto* shape = object.AsTriShape();
		if (!shape) {
			if (auto* node = object.AsNode()) {
				for (auto& child : node->children) {
					if (child) { GatherRedsandBladeTip(*child, local, depth - 1, vertices, bounds, remaining); }
				}
			}
			return;
		}

		const auto& geometry = shape->GetGeometryRuntimeData();
		if (geometry.skinInstance || shape->GetType().get() != RE::BSGeometry::Type::kTriShape) { return; }
		if (const auto* shader = geometry.shaderProperty.get(); shader &&
			shader->flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) { return; }
		const auto& model = shape->GetModelData().modelBound;
		const auto center = local * model.center;
		const float length = center.Length();
		if (length > 1.f && std::isfinite(model.radius) && model.radius > 0.f) {
			bounds.Consider(SwingVector(center * (1.f + std::abs(local.scale) * model.radius / length)));
		}

		const auto* renderer = geometry.rendererData;
		auto descriptor = geometry.vertexDesc;
		const auto stride = static_cast<unsigned>(std::bit_cast<std::uint64_t>(descriptor) & 0xFu) * 4u;
		if (!renderer || !renderer->rawVertexData || !descriptor.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) ||
			stride < 16u || stride != descriptor.GetSize() || !(renderer->vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_VERTEX)) { return; }
		const auto rawStride = static_cast<unsigned>(std::bit_cast<std::uint64_t>(renderer->vertexDesc) & 0xFu) * 4u;
		if (rawStride != stride) { return; }
		const unsigned count = std::min<unsigned>(shape->GetTrishapeRuntimeData().vertexCount, remaining);
		remaining -= count;
		auto inside = 0u;
		RedsandSwingTrail::BladeTip current;
		for (unsigned i = 0; i < count; ++i) {
			std::array<float, 3> coord{};
			std::memcpy(coord.data(), renderer->rawVertexData + i * stride, sizeof(coord));
			const RE::NiPoint3 point{coord[0], coord[1], coord[2]};
			const float distance = (point - model.center).Length();
			if (std::isfinite(distance) && distance <= model.radius * 1.05f + .25f) {
				++inside;
				current.Consider(SwingVector(local * point));
			}
		}
		if (inside && inside >= count * 9u / 10u) { vertices.Consider(current.point); }
	}

	RE::NiParticleSystem* FindRedsandSwingSystem(RE::NiAVObject* root)
	{
		RE::NiParticleSystem* result = nullptr;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
			if (geometry->name == "RedsandParticles_Drain_v1"sv) {
				result = netimmerse_cast<RE::NiParticleSystem*>(geometry);
				if (result) { return RE::BSVisit::BSVisitControl::kStop; }
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return result;
	}

	const RE::NiPointer<RE::NiNode>& RedsandSwingTemplate()
	{
		static const auto model = [] {
			RE::NiPointer<RE::NiNode> loaded;
			if (RE::BSModelDB::Demand(redsandSwingMesh, loaded, {}) != RE::BSResource::ErrorCode::kNone) { loaded.reset(); }
			return loaded;
		}();
		return model;
	}

	bool HasIndependentRedsandSwingData(RE::NiParticleSystem& system, RE::NiParticleSystem& source)
	{
		const auto* data = system.GetParticlesRuntimeData().particleData.get();
		const auto* original = source.GetParticlesRuntimeData().particleData.get();
		if (!data || !original || data == original ||
			system.GetGeometryRuntimeData().shaderProperty == source.GetGeometryRuntimeData().shaderProperty) { return false; }
		const auto& vertices = data->GetParticlesRuntimeData();
		const auto& shared = original->GetParticlesRuntimeData();
		return vertices.positions && vertices.positions != shared.positions && vertices.color && vertices.color != shared.color &&
			vertices.radii && vertices.radii != shared.radii && vertices.sizes && vertices.sizes != shared.sizes;
	}
}

void AttackTrail::WarmRedsandSwingModel()
{
	(void)RedsandSwingTemplate();
}

void AttackTrail::RetireRedsandSwingTrail()
{
	if (redsandSwingRoot) {
		redsandSwingRoot->SetAppCulled(true);
		if (auto* parent = redsandSwingRoot->parent) { parent->DetachChild(redsandSwingRoot.get()); }
		redsandSwingRoot.reset();
	}
	redsandSwingActorRoot.reset();
	if (trailParticle) {
		if (auto root = trailParticle->particleObject) { root->SetAppCulled(true); }
		trailParticle->age = trailParticle->lifetime + 1.f;
	}
	bActive = false;
}

bool AttackTrail::CreateRedsandSwingRoot(RE::Actor& actor)
{
	auto* actorRoot = actor.Get3D();
	if (!actorRoot || !actorRoot->AsNode()) { return false; }
	const auto& prototype = RedsandSwingTemplate();
	auto* source = prototype ? FindRedsandSwingSystem(prototype.get()) : nullptr;
	if (!source) { return false; }
	RE::NiPointer<RE::NiObject> clone(prototype->Clone());
	const auto independentRoot = [&]() -> RE::NiNode* {
		auto* root = clone && clone.get() != prototype.get() ? clone->AsNode() : nullptr;
		auto* system = root ? FindRedsandSwingSystem(root) : nullptr;
		return system && system != source && HasIndependentRedsandSwingData(*system, *source) ? root : nullptr;
	};
	auto* root = independentRoot();
	if (!root) {
		clone.reset();
		prototype->CreateDeepCopy(clone);
		root = independentRoot();
	}
	if (!root) { return false; }
	redsandSwingRoot.reset(root);
	redsandSwingActorRoot.reset(actorRoot);
	root->SetAppCulled(true);
	root->local = RE::NiTransform{};
	actorRoot->AsNode()->AttachChild(root, true);
	return true;
}

bool AttackTrail::UpdateRedsandSwingTrail(float delta)
{
	if (!std::isfinite(delta) || delta <= 0.f) { return bActive; }
	const auto retire = [&]() {
		RetireRedsandSwingTrail();
		return false;
	};
	auto actor = actorHandle.get();
	auto* actorRoot = actor ? actor->Get3D() : nullptr;
	redsandConfig = RedsandSettings::Get();
	RedsandSettings::ApplyMeleeAppearance(redsandConfig, redsandEnchantmentKey, redsandEnchantmentFallback);
	if (!bActive || !redsandSwingLease.Acquire()) { return retire(); }
	const bool scope = actorRoot && Settings::bDisplayTrails && Settings::bAttackCollisionsEnabled &&
		!Settings::bDisableMod && redsandConfig.swordEnabled &&
		(actor->IsPlayerRef() ? redsandConfig.swordPlayer : redsandConfig.swordNPC) &&
		(!redsandSwingActorRoot || actorRoot == redsandSwingActorRoot.get());
	if (!bExpired) {
		bExpired = !scope || !redsandConfig.swordReleasedTrail || !collisionParentNode || !collisionNode ||
			!PrecisionHandler::GetSingleton()->GetAttackCollision(actorHandle, collisionNode.get());
	}
	const bool emitting = !bExpired;
	redsandSwingSimulation.AdvanceFrame(delta);
	if (emitting) {
		const auto span=Redsand::Weapons::MeleeEmissionSpan(redsandWeaponProfile);
		const float fallbackReach = std::abs(scale) * 100.f * span.end;
		if (!redsandBladeMeasured) {
			redsandBladeMeasured = true;
			RedsandSwingTrail::BladeTip vertices, bounds;
			unsigned remaining = 32768;
			for (auto& child : collisionParentNode->children) {
				if (child && child.get() != collisionNode.get()) {
					GatherRedsandBladeTip(*child, RE::NiTransform{}, 12, vertices, bounds, remaining);
				}
			}
			const auto& selected = vertices.reachSquared > 0.f ? vertices : bounds;
			if (selected.reachSquared > 0.f) {
				redsandBladeTip = RE::NiPoint3{selected.point.x, selected.point.y, selected.point.z} * span.end;
				redsandBladeSource = vertices.reachSquared > 0.f ? "mesh-vertices" : "mesh-bounds";
			}
		}
		const auto pose = [&](const RE::NiTransform& transform) {
			const auto& base = transform.translate;
			const auto tip = redsandBladeTip ? transform * *redsandBladeTip :
				base + transform.rotate * RE::NiPoint3{0.f, fallbackReach, 0.f};
			return RedsandSwingTrail::Pose{SwingVector(base), SwingVector(tip)};
		};
		if (redsandSwingStart) {
			redsandSwingSimulation.Seed(pose(*redsandSwingStart));
			redsandSwingStart.reset();
		}
		const auto current = pose(collisionParentNode->world);
		const auto parameters = RedsandSwingTrail::CompactStroke({redsandConfig.swordRibbonLifetime,redsandConfig.swordParticleLifetime,
			redsandConfig.swordRibbonWidth,redsandConfig.swordTrailMomentum,redsandConfig.swordDensity,redsandConfig.swordSize,redsandConfig.swordSpread,
			redsandConfig.swordRibbonFadeOut,redsandConfig.swordParticleFadeOut,redsandConfig.swordParticleGravity});
		redsandSwingSimulation.Sample(current.base, current.tip, delta, parameters);
	}
	if (!emitting && redsandSwingSimulation.EndAttack() && redsandActorIsPlayer && redsandConfig.diagnostics) {
		float arc = 0.f;
		float grains = 0.f;
		for (const auto& particle : redsandSwingSimulation.Particles()) {
			auto& remaining = particle.ribbon ? arc : grains;
			remaining = std::max(remaining, particle.lifetime - particle.age);
		}
		RedsandLog::info("Redsand arc emission ended: particles={} arcRemaining={} grainsRemaining={}",
			redsandSwingSimulation.Particles().size(), arc, grains);
	}
	if (redsandSwingSimulation.Empty() && !redsandSwingSpawnAttempted) { return emitting ? true : retire(); }
	if (!emitting && redsandSwingSimulation.Empty()) { return retire(); }

	if (!redsandSwingSpawnAttempted) {
		redsandSwingSpawnAttempted = true;
		if (!CreateRedsandSwingRoot(*actor)) {
			if (auto* cell = actor->GetParentCell()) {
				const auto& transform = collisionParentNode->world;
				trailParticle.reset(RE::BSTempEffectParticle::Spawn(
					cell, 10.f, redsandSwingMesh, transform.rotate, transform.translate, 1.f, 7, nullptr));
			}
		}
	}
	RE::NiAVObject* root = redsandSwingRoot.get();
	if (!root && trailParticle) { root = trailParticle->particleObject.get(); }
	const auto wait = [&]() {
		redsandSwingWaiting += delta;
		if (!redsandSwingPublished) { redsandSwingSimulation.DiscardParticles(); }
		return emitting && (redsandSwingRoot || trailParticle) && redsandSwingWaiting < .15f ? true : retire();
	};
	if (!root) { return wait(); }
	if (!redsandChecked) {
		if (auto* system = FindRedsandSwingSystem(root)) { redsandSystems.emplace_back(system); }
		redsandChecked = true;
	}
	if (redsandSystems.empty()) { return retire(); }
	auto* system=redsandSystems.front().get();
	auto* data=system->GetParticlesRuntimeData().particleData.get();
	if (!data) { return wait(); }
	auto& vertices=data->GetParticlesRuntimeData();
	if (!vertices.positions || !vertices.color || !vertices.radii || !vertices.sizes || !vertices.maxNumVertices) { return retire(); }
	if (!redsandSwingReady) {
		RE::BSVisit::TraverseScenegraphObjects(root,[](RE::NiAVObject* object) {
			for (auto controller=object->GetControllers();controller;controller=controller->GetNext()) { controller->flags.reset(RE::NiTimeController::Flag::kActive); }
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		for (auto& modifier:system->GetParticleSystemRuntimeData().modifierList) { if (modifier) { modifier->SetActive(false); } }
		RE::BSVisit::TraverseScenegraphGeometries(root,[&](RE::BSGeometry* geometry) {
			geometry->SetAppCulled(geometry!=system);return RE::BSVisit::BSVisitControl::kContinue;
		});
		vertices.numVertices=0;
		redsandSwingReady=true;
	}
	if (redsandSwingRoot) {
		RE::NiUpdateData update{};
		root->Update(update);
	}
	if (!std::isfinite(system->world.scale) || system->world.scale <= .0001f) { return retire(); }
	redsandSwingWaiting=0.f;
	redsandSwingSimulation.SetRenderCapacity(vertices.maxNumVertices);
	const auto& particles=redsandSwingSimulation.Particles();
	const auto count=static_cast<std::uint16_t>(std::min<std::size_t>(particles.size(),vertices.maxNumVertices));
	const auto inverse=system->world.Invert();
	RE::NiPoint3 low{0.f,0.f,0.f},high=low;
	float margin=0.f;
	unsigned arcCount = 0;
	auto& runtime=data->GetPSysRuntimeData();
	for (std::uint16_t i=0;i<count;++i) {
		const auto& p=particles[i];const auto position=inverse*RE::NiPoint3{p.position.x,p.position.y,p.position.z};
		arcCount += p.ribbon ? 1u : 0u;
		vertices.positions[i]=position;vertices.color[i]={p.intensity,p.intensity,p.intensity,p.Alpha()};
		const float width = p.ribbon ? redsandConfig.swordRibbonWidth : 1.f;
		vertices.radii[i]=p.Size()*width/system->world.scale;vertices.sizes[i]=1.f;
		margin=std::max(margin,vertices.radii[i]);
		if (vertices.rotationAngles) { vertices.rotationAngles[i]=0.f; }
		if (vertices.rotationAxes) { vertices.rotationAxes[i]={0.f,0.f,1.f}; }
		if (vertices.rotations) { vertices.rotations[i]=RE::NiQuaternion{1.f,0.f,0.f,0.f}; }
		if (vertices.textureIndices) { vertices.textureIndices[i]=0; }
		if (runtime.rotationSpeeds) { runtime.rotationSpeeds[i]=0.f; }
		if (runtime.particleInfo) { runtime.particleInfo[i]={};runtime.particleInfo[i].age=p.age;runtime.particleInfo[i].lifespan=p.lifetime; }
		if (!i) { low=high=position; }
		low.x=std::min(low.x,position.x);low.y=std::min(low.y,position.y);low.z=std::min(low.z,position.z);
		high.x=std::max(high.x,position.x);high.y=std::max(high.y,position.y);high.z=std::max(high.z,position.z);
	}
	vertices.numVertices=count;
	auto& ps=system->GetParticleSystemRuntimeData();
	ps.isWorldspace=false;ps.dynamicBounds=false;ps.resetSystem=false;runtime.addedParticleCount=0;runtime.addedParticlesBase=0;
	vertices.bound.center=(low+high)*.5f;vertices.bound.radius=(high-low).Length()*.5f+margin+1.f;
	system->GetModelData().modelBound=vertices.bound;system->UpdateWorldBound();
	unsigned depth=0;
	for (auto* parent=system->parent;parent && depth<32;parent=parent->parent,++depth) { parent->UpdateWorldBound();if (parent==root) { break; } }
	ApplyColorSettings(system,!bAppliedTrailColorSettings,false);bAppliedTrailColorSettings=true;
	system->SetAppCulled(count==0);root->SetAppCulled(false);
	if (auto* fade=root->AsFadeNode()) { fade->currentFade=1.f; }
	if (trailParticle) { trailParticle->lifetime=std::max(trailParticle->lifetime,trailParticle->age+1.f); }
	if (count && !redsandSwingPublished) {
		redsandSwingPublished = true;
		if (redsandActorIsPlayer && redsandConfig.diagnostics) {
			const auto tip = redsandBladeTip.value_or(RE::NiPoint3{});
			RedsandLog::info("Redsand arc first frame: window=collision renderer={} particles={} arc={} grains={} source={} localTip={}/{}/{}",
				redsandSwingRoot ? "attached" : "temporary", count, arcCount, count - arcCount, redsandBladeSource, tip.x, tip.y, tip.z);
		}
	}
	return true;
}
