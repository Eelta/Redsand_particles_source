void AttackTrail::LogRedsandParticles(RE::BSGeometry* geometry, std::string_view stage)
{
	if (stage == "initialized"sv && geometry && !redsandLegacyTrail && geometry->name == "RedsandParticles_Drain_v1"sv) {
		static std::array<std::atomic<unsigned>, 4> actualColors{};
        const auto tint = Redsand::Weapons::defaultTrailTint;
        const bool blue = redsandInheritedColor.red == tint.red && redsandInheritedColor.green == tint.green && redsandInheritedColor.blue == tint.blue;
        const auto partition = (redsandActorIsPlayer ? 2u : 0u) + (blue ? 1u : 0u);
        if (actualColors[partition].fetch_add(1) < 6) {
			auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = shader ? shader->GetMaterial() : nullptr;
			if (material) { logger::info("Redsand rendered melee: rgb={}/{}/{} glow={} alpha={} texture={}",
				material->baseColor.red, material->baseColor.green, material->baseColor.blue,
				material->baseColorScale, material->baseColor.alpha, material->sourceTexturePath.c_str()); }
		}
	}
	if (!redsandConfig.diagnostics || redsandLegacyTrail || !geometry || geometry->name != "RedsandParticles_Drain_v1"sv) { return; }
	const float clock = currentTime + redsandDrain.elapsed;
	if (stage == "frame"sv && clock < redsandNextDiagnostic) { return; }
	redsandNextDiagnostic = clock + 1.f;
	auto property = geometry->GetGeometryRuntimeData().shaderProperty;
	auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(property.get());
	auto* material = shader ? skyrim_cast<RE::BSEffectShaderMaterial*>(shader->material) : nullptr;
	const auto actor = actorHandle.get();
	if (!material) {
		RedsandLog::info("Redsand diagnostic: stage={} actor={:08X} trail={} geometry={} shader={} missingMaterial=true",
			stage, actor ? actor->GetFormID() : 0, fmt::ptr(this), fmt::ptr(geometry), fmt::ptr(shader));
		return;
	}
	std::uint32_t live = 0, sampled = 0;
	float alphaMin = 1.f, alphaMax = 0.f, colorPeak = 0.f;
	if (auto* system = netimmerse_cast<RE::NiParticleSystem*>(geometry)) {
		if (auto data = system->GetParticlesRuntimeData().particleData) {
			const auto& particles = data->GetParticlesRuntimeData();
			live = particles.numVertices;
			if (particles.color) {
				sampled = std::min<std::uint32_t>({ live, particles.maxNumVertices, 256 });
				for (std::uint32_t i = 0; i < sampled; ++i) {
					const auto& color = particles.color[i];
					alphaMin = std::min(alphaMin, color.alpha);
					alphaMax = std::max(alphaMax, color.alpha);
					colorPeak = std::max({ colorPeak, color.red, color.green, color.blue });
				}
			}
		}
	}
	const auto& color = material->baseColor;
	const float peak = std::max({ color.red, color.green, color.blue, 0.f });
	const float expected = peak > 0.f ? 14.04f * redsandConfig.swordBrightness / peak : 0.f;
	auto* root = trailParticle ? trailParticle->particleObject.get() : nullptr;
	auto* fade = root ? root->AsFadeNode() : nullptr;
	auto* actorRoot = actor ? actor->Get3D() : nullptr;
	auto* actorFade = actorRoot ? actorRoot->AsFadeNode() : nullptr;
	RedsandLog::info("Redsand diagnostic: stage={} actor={:08X} player={} trail={} geometry={} shader={} material={} root={} profile={} clock={} drain={} expired={} ctorPlayer={} ctorAllowed={} scope={}/{}/{} slider={} rgb={}/{}/{} alpha={} originalAlpha={} scale={} expectedScale={} visibility={} rootFade={} actorFade={} live={} sampled={} particleAlpha={}/{} particleColorPeak={} texture={}",
		stage, actor ? actor->GetFormID() : 0, actor && actor->IsPlayerRef(), fmt::ptr(this), fmt::ptr(geometry), fmt::ptr(shader), fmt::ptr(material), fmt::ptr(root),
		static_cast<unsigned>(redsandWeaponProfile), clock, redsandDrain.elapsed, bExpired, redsandActorIsPlayer, redsandScopeAllowed,
		redsandConfig.swordEnabled, redsandConfig.swordPlayer, redsandConfig.swordNPC, redsandConfig.swordBrightness,
		color.red, color.green, color.blue, color.alpha, originalTrailAlpha.value_or(-1.f), material->baseColorScale, expected,
		visibilityPercent, fade ? fade->currentFade : -1.f, actorFade ? actorFade->currentFade : -1.f,
		live, sampled, sampled ? alphaMin : -1.f, sampled ? alphaMax : -1.f, colorPeak, material->sourceTexturePath.c_str());
}
