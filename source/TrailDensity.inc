const auto redsandRequestedMesh = redsandConfig.diagnostics ? trailMeshPath : std::string{};
		trailMeshPath = Redsand::Weapons::ResolveMeleeMesh(redsandScopeAllowed, trailMeshPath, Settings::attackTrailMeshPath);
		if (!redsandLegacyTrail && (trailMeshPath == Settings::attackTrailMeshPath || trailMeshPath == "Effects/WeaponTrails/AttackTrailMagic.nif")) {
			trailMeshPath = "Effects/WeaponTrails/ElementalDesert/DefaultYellow.nif";
		}
		if (!redsandLegacyTrail && !baseColorOverride) {
			baseColorOverride = RE::NiColorA{ 0.025f, 0.080f, 0.650f, 1.f };
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
		if (redsandConfig.diagnostics) {
			RedsandLog::info("Redsand melee scope: actor={:08X} player={} weapon={:08X} weaponType={} profile={} enabled={} playerEnabled={} npcEnabled={} allowed={} requested={} resolved={}",
				redsandActor ? redsandActor->GetFormID() : 0, redsandActorIsPlayer,
				redsandWeapon ? redsandWeapon->GetFormID() : 0,
				redsandWeapon ? static_cast<unsigned>(redsandWeapon->GetWeaponType()) : 0,
				static_cast<unsigned>(redsandWeaponProfile), redsandConfig.swordEnabled,
				redsandConfig.swordPlayer, redsandConfig.swordNPC, redsandScopeAllowed, redsandRequestedMesh, trailMeshPath);
		}
