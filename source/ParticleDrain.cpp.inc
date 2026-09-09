bool AttackTrail::UpdateRedsandParticles(float a_deltaTime)
{
	if (!trailParticle || !trailParticle->particleObject) {
		logger::warn("Redsand drain v1: object lost instance={} elapsed={}", fmt::ptr(this), redsandDrain.elapsed);
		bActive = false;
		return false;
	}
	std::uint32_t liveParticles = 0;
	for (auto& system : redsandSystems) {
		for (auto controller = system->GetControllers(); controller; controller = controller->GetNext()) {
			for (auto type = controller->GetRTTI(); type; type = type->GetBaseRTTI()) {
				if (std::string_view(type->GetName()) == "NiPSysEmitterCtlr"sv) {
					controller->flags.reset(RE::NiTimeController::Flag::kActive);
					break;
				}
			}
		}
		for (auto& entry : system->GetParticleSystemRuntimeData().modifierList) {
			if (auto modifier = entry.get()) {
				for (auto type = modifier->GetRTTI(); type; type = type->GetBaseRTTI()) {
					if (std::string_view(type->GetName()) == "NiPSysEmitter"sv) {
						modifier->SetActive(false);
						break;
					}
				}
			}
		}
		if (auto data = system->GetParticlesRuntimeData().particleData) {
			liveParticles += data->GetParticlesRuntimeData().numVertices;
		}
	}
	redsandDrain.Advance(a_deltaTime);
	const float safetyAlpha = redsandDrain.SafetyAlpha();
	visibilityPercent = safetyAlpha;
	RE::BSVisit::TraverseScenegraphGeometries(trailParticle->particleObject.get(), [&](RE::BSGeometry* geometry) {
		return ApplyColorSettings(geometry, !bAppliedTrailColorSettings, safetyAlpha < 1.f);
	});
	bAppliedTrailColorSettings = true;
	if (auto fade = trailParticle->particleObject->AsFadeNode()) {
		fade->currentFade = 1.f;
	}
	if (redsandDrain.CanRetire(liveParticles)) {
		trailParticle->age = trailParticle->lifetime + 1.f;
		bActive = false;
		return false;
	}
	return true;
}

