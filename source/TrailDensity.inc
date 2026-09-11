		if (redsandLegacyTrail) {
			if (trailMeshPath == "Effects/WeaponTrails/AttackTrail.nif") {
				trailMeshPath = "Effects/WeaponTrails/PlayerOriginal/AttackTrail.nif";
			} else if (trailMeshPath == "Effects/WeaponTrails/AttackTrailMagic.nif") {
				trailMeshPath = "Effects/WeaponTrails/PlayerOriginal/AttackTrailMagic.nif";
			}
			if (!baseColorOverride) {
				baseColorOverride = RE::NiColorA{ 0.055f, 0.216f, 0.287f, 1.f };
			}
		}
		// Density ESPs select equivalent particle meshes; original ribbons are exempt.
		static auto* particleDensity = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(0x80B, "PrecisionParticleMatch.esp");
		if (particleDensity && !redsandLegacyTrail) {
			const float percent = particleDensity->value;
			if (percent == 75.f || percent == 50.f || percent == 25.f || percent == 5.f) {
				bool managedMesh = trailMeshPath == "Effects/WeaponTrails/AttackTrail.nif";
				for (const auto* color : { "Blue", "Bound", "DefaultYellow", "Fire", "Frost", "Green", "Purple", "Red", "Shock", "Sun" }) {
					managedMesh = managedMesh || trailMeshPath == std::string("Effects/WeaponTrails/ElementalDesert/") + color + ".nif";
				}
				if (managedMesh) {
					trailMeshPath.insert(trailMeshPath.find_last_of('/') + 1, "Density" + std::to_string(static_cast<int>(percent)) + "/");
				}
			}
		}
