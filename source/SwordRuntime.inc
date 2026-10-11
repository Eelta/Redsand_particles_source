void AttackTrail::CacheRedsandEmissionMeshes(RE::NiAVObject* root)
{
	redsandEmitterMeshes.clear();
	redsandTipSample = false;
	if (redsandLegacyTrail || !root) { return; }

	RE::BSVisit::TraverseScenegraphObjects(root, [&](RE::NiAVObject* object) {
		if (object->name == "Trail Mesh" || object->name == "Trailmesh2") {
			redsandEmitterMeshes.push_back({ RE::NiPointer<RE::NiAVObject>(object), object->local });
		}
		return RE::BSVisit::BSVisitControl::kContinue;
	});
}

void AttackTrail::UpdateRedsandEmission()
{
	if (!redsandManaged) { redsandTipSample = false; return; }
	RE::NiTransform tipSpan;
	const bool useTip = redsandConfig.swordTip && collisionParentNode;
	if (useTip) {
		const float reach = std::abs(scale) * 100.f;

		const auto currentTip = collisionParentNode->world.translate +
			collisionParentNode->world.rotate * RE::NiPoint3{ 0.f, reach, 0.f };
		tipSpan.translate = currentTip;
		tipSpan.rotate = collisionParentNode->world.rotate * weaponRotation;
		tipSpan.scale = std::max(std::abs(scale) * 0.01f, 0.0001f);
		if (redsandTipSample) {
			auto direction = currentTip - redsandPreviousTip;
			const float distance = direction.Length();

			if (std::isfinite(distance) && distance > 0.01f && distance <= std::max(100.f, reach * 4.f)) {
				direction.Unitize();
				const RE::NiPoint3 reference = std::abs(direction.z) < 0.95f ?
					RE::NiPoint3{ 0.f, 0.f, 1.f } : RE::NiPoint3{ 0.f, 1.f, 0.f };
				auto side = reference.Cross(direction);
				side.Unitize();
				const auto up = direction.Cross(side);

				tipSpan.rotate.entry[0][0] = direction.x; tipSpan.rotate.entry[0][1] = side.x; tipSpan.rotate.entry[0][2] = up.x;
				tipSpan.rotate.entry[1][0] = direction.y; tipSpan.rotate.entry[1][1] = side.y; tipSpan.rotate.entry[1][2] = up.y;
				tipSpan.rotate.entry[2][0] = direction.z; tipSpan.rotate.entry[2][1] = side.z; tipSpan.rotate.entry[2][2] = up.z;
				tipSpan.translate = redsandPreviousTip;
				tipSpan.scale = distance / 10.042167f;
			}
		}
		redsandPreviousTip = currentTip;
		redsandTipSample = true;
	} else {
		redsandTipSample = false;
	}
	for (auto& emitter : redsandEmitterMeshes) {
		auto* geometry = emitter.geometry.get();

		if (geometry->parent && geometry->parent->parent) {
			geometry->parent->world = geometry->parent->parent->world * geometry->parent->local;
		}
		geometry->local = emitter.originalLocal;
		if (geometry->parent) { geometry->world = geometry->parent->world * geometry->local; }
		auto transformed = geometry->world;
		if (useTip) {
			transformed = tipSpan;
		}
		else if (redsandWeaponProfile != Redsand::Weapons::Profile::Default && collisionParentNode) {
			const auto span = Redsand::Weapons::MeleeEmissionSpan(redsandWeaponProfile);
			const float reach = std::abs(scale) * 100.f;
			transformed.rotate = collisionParentNode->world.rotate * weaponRotation;
			transformed.translate = collisionParentNode->world.translate + collisionParentNode->world.rotate * RE::NiPoint3{ 0.f, reach * span.start, 0.f };
			transformed.scale = std::max(reach * (span.end - span.start) / 10.042167f, 0.0001f);
		}
		Redsand::Emission::NarrowSource(transformed.rotate, redsandConfig.swordSpread);
		Utils::UpdateNodeTransformLocal(geometry, transformed);
		geometry->world = transformed;
	}
}
