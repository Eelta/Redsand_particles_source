const auto redsandRequestedMesh = redsandConfig.diagnostics ? trailMeshPath : std::string{};
		bool redsandRequestedMeshAvailable = true;
		if (!redsandScopeAllowed && Redsand::Weapons::IsLEDWeaponMesh(trailMeshPath)) {
			RE::BSResourceNiBinaryStream redsandRibbonResource("Meshes\\" + trailMeshPath);
			redsandRequestedMeshAvailable = redsandRibbonResource.good();
		}
		trailMeshPath = Redsand::Weapons::ResolveMeleeMesh(redsandScopeAllowed, trailMeshPath, Settings::attackTrailMeshPath,
			redsandRequestedMeshAvailable);
		if (redsandNativeOnly && trailMeshPath.empty()) { trailMeshPath = "Effects/WeaponTrails/AttackTrail.nif"; }
		if (!redsandLegacyTrail && (trailMeshPath == Settings::attackTrailMeshPath || trailMeshPath == "Effects/WeaponTrails/AttackTrailMagic.nif")) {
			trailMeshPath = "RedsandParticles/Default.nif";
		}
		if (!redsandLegacyTrail && !baseColorOverride) {
			const auto tint = Redsand::Weapons::defaultTrailTint;
			baseColorOverride = RE::NiColorA{ tint.red, tint.green, tint.blue, 1.f };
		}
		if (!redsandLegacyTrail) {
			trailMeshPath = Redsand::Weapons::MapMeshPath(trailMeshPath, redsandWeaponProfile);
			if (redsandConfig.diagnostics) {
				static std::atomic<std::uint32_t> loggedProfiles{ 0 };
				const auto bit = 1u << static_cast<unsigned>(redsandWeaponProfile);
				if (!(loggedProfiles.fetch_or(bit) & bit)) {
					const auto folder = Redsand::Weapons::ProfileFolder(redsandWeaponProfile);
					RedsandLog::info("Redsand melee selection: weapon={:08X} profile={} mesh={}",
						redsandWeapon ? redsandWeapon->GetFormID() : 0,
						folder.empty() ? "Default"sv : folder, trailMeshPath);
				}
			}
		}
		redsandReleasedSelected = redsandReleasedSelected && Redsand::Weapons::IsParticleMesh(trailMeshPath);
		if (redsandReleasedSelected) { trailMeshPath = "RedsandSwingTrail/Arc.nif"; }
		redsandParticleSelected = Redsand::Weapons::IsParticleMesh(trailMeshPath);
		if (redsandConfig.diagnostics) {
			RedsandLog::info("Redsand melee scope: actor={:08X} player={} weapon={:08X} weaponType={} profile={} enabled={} playerEnabled={} npcEnabled={} allowed={} requested={} resolved={}",
				redsandActor ? redsandActor->GetFormID() : 0, redsandActorIsPlayer,
				redsandWeapon ? redsandWeapon->GetFormID() : 0,
				redsandWeapon ? static_cast<unsigned>(redsandWeapon->GetWeaponType()) : 0,
				static_cast<unsigned>(redsandWeaponProfile), redsandConfig.swordEnabled,
				redsandConfig.swordPlayer, redsandConfig.swordNPC, redsandScopeAllowed, redsandRequestedMesh, trailMeshPath);
		}
