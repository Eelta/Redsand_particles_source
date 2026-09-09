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
			trailParticle->lifetime = std::max(trailParticle->lifetime, trailParticle->age + 7.f);
			if (bExpired) {
				return UpdateRedsandParticles(a_deltaTime);
			}
		}
