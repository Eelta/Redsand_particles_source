	if (redsandNativeOnly) {
		if (!bActive || !RedsandSettings::Get().swordKeepPrecisionTrail || !Settings::bDisplayTrails ||
			!actorHandle.get() || !collisionNode || !collisionParentNode || !trailParticle ||
			trailParticle->age > trailParticle->lifetime) {
			RetireRedsandNativeTrail();
			return false;
		}
		auto* root = trailParticle->particleObject ? trailParticle->particleObject->GetObjectByName("TrailRoot"sv) : nullptr;
		auto* node = root ? root->AsNode() : nullptr;
		if (!node || node->children.empty()) {
			if (std::isfinite(a_deltaTime) && a_deltaTime > 0.f) { redsandNativePendingTime += a_deltaTime; }
			if (redsandNativePendingTime >= 2.f) {
				RetireRedsandNativeTrail();
				return false;
			}
			return true;
		}
	}
