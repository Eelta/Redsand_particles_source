bool AttackTrail::UpdateRedsandSwingTrail(float delta)
{
	if (!std::isfinite(delta) || delta <= 0.f) { return bActive; }
	const auto retire=[&]() { if (trailParticle) { trailParticle->age=trailParticle->lifetime+1.f; } bActive=false;return false; };
    if (!redsandSwingLease.Acquire()) { return retire(); }
	redsandSwingSimulation.Advance(delta);
	if (!bExpired && redsandConfig.swordReleasedTrail && collisionParentNode) {
		const auto span=Redsand::Weapons::MeleeEmissionSpan(redsandWeaponProfile);
		const float reach=std::abs(scale)*100.f;
		const auto base=collisionParentNode->world.translate+collisionParentNode->world.rotate*RE::NiPoint3{0.f,reach*span.start,0.f};
		const auto tip=collisionParentNode->world.translate+collisionParentNode->world.rotate*RE::NiPoint3{0.f,reach*span.end,0.f};
		const RedsandSwingTrail::Parameters parameters{redsandConfig.swordRibbonLifetime,redsandConfig.swordParticleLifetime,
			redsandConfig.swordRibbonWidth,redsandConfig.swordTrailMomentum,redsandConfig.swordDensity,redsandConfig.swordSize,redsandConfig.swordSpread,
			redsandConfig.swordRibbonFadeOut,redsandConfig.swordParticleFadeOut,redsandConfig.swordParticleGravity};
		redsandSwingSimulation.Sample({base.x,base.y,base.z},{tip.x,tip.y,tip.z},delta,parameters);
	} else { redsandSwingSimulation.Break(); }
    if ((bExpired || !redsandConfig.swordReleasedTrail) && redsandSwingSimulation.Empty()) { return retire(); }

	if (!trailParticle) { return retire(); }
	if (!trailParticle->particleObject) { redsandSwingWaiting+=delta;return redsandSwingWaiting<2.f ? true : retire(); }
	auto* root=trailParticle->particleObject.get();
	if (redsandSystems.empty()) { redsandSwingWaiting+=delta; return redsandSwingWaiting<2.f ? true : retire(); }
	auto* system=redsandSystems.front().get();
	auto* data=system->GetParticlesRuntimeData().particleData.get();
	if (!data) { redsandSwingWaiting+=delta; return redsandSwingWaiting<2.f ? true : retire(); }
	auto& vertices=data->GetParticlesRuntimeData();
	if (!vertices.positions || !vertices.color || !vertices.radii || !vertices.sizes || !vertices.maxNumVertices ||
		!std::isfinite(system->world.scale) || system->world.scale<=.0001f) { redsandSwingWaiting+=delta; return redsandSwingWaiting<2.f ? true : retire(); }
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
	redsandSwingWaiting=0.f;
	const auto& particles=redsandSwingSimulation.Particles();
	const auto count=static_cast<std::uint16_t>(std::min<std::size_t>(particles.size(),vertices.maxNumVertices));
	const auto inverse=system->world.Invert();
	RE::NiPoint3 low{0.f,0.f,0.f},high=low;
	float margin=0.f;
	auto& runtime=data->GetPSysRuntimeData();
	for (std::uint16_t i=0;i<count;++i) {
		const auto& p=particles[i];const auto position=inverse*RE::NiPoint3{p.position.x,p.position.y,p.position.z};
		vertices.positions[i]=position;vertices.color[i]={p.intensity,p.intensity,p.intensity,p.Alpha()};
		vertices.radii[i]=p.Size()/system->world.scale;vertices.sizes[i]=1.f;
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
	trailParticle->lifetime=std::max(trailParticle->lifetime,trailParticle->age+1.f);
	if ((bExpired || !redsandConfig.swordReleasedTrail) && particles.empty()) { trailParticle->age=trailParticle->lifetime+1.f;bActive=false;return false; }
	return true;
}
