		if (!redsandChecked && trailParticle && trailParticle->particleObject) {
			RE::BSVisit::TraverseScenegraphGeometries(trailParticle->particleObject.get(), [&](RE::BSGeometry* geometry) {
				if (geometry->name == "RedsandParticles_Drain_v1"sv) {
					if (auto system = netimmerse_cast<RE::NiParticleSystem*>(geometry)) {
						redsandSystems.emplace_back(system);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			redsandManaged = !redsandSystems.empty();
			redsandChecked = true;
		}
		if (redsandManaged) {
			// The optional ESPs override this condition-only perk; subject is the attacker.
			static auto* particleScope = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSPerk>(0x80A, "PrecisionParticleMatch.esp");
			auto actor = actorHandle.get();
			if (particleScope && actor && !particleScope->perkConditions.IsTrue(actor.get(), nullptr)) {
				trailParticle->particleObject->SetAppCulled(true);
				trailParticle->age = trailParticle->lifetime + 1.f;
				bActive = false;
				return false;
			}
			trailParticle->lifetime = std::max(trailParticle->lifetime, trailParticle->age + 7.f);
			if (bExpired) {
				return UpdateRedsandParticles(a_deltaTime);
			}
		}
