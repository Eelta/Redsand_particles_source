redsandConfig = RedsandSettings::Get();
		auto* redsandWeapon = a_weaponItem && a_weaponItem->object ? a_weaponItem->object->As<RE::TESObjectWEAP>() : nullptr;
		redsandWeaponProfile = Redsand::Weapons::ForWeapon(redsandWeapon);
		const bool redsandStaff = redsandWeapon && redsandWeapon->GetWeaponType() == RE::WEAPON_TYPE::kStaff;
		const auto redsandActor = a_actorHandle.get();
		redsandEnchantmentKey = RedsandEnchantmentProfiles::Key(a_weaponItem, redsandActor ? &*redsandActor : nullptr, a_bIsLeftHand);
		redsandEnchantmentFallback.clear();
		RedsandSettings::ApplyMeleeAppearance(redsandConfig, redsandEnchantmentKey, "");
		redsandActorIsPlayer = redsandActor && redsandActor->IsPlayerRef();
		const bool allowRedsandLayers = !redsandNativeOnly && redsandActor;
		redsandOrdinaryAllowed = allowRedsandLayers && Redsand::Weapons::OrdinaryMeleeParticlesAllowed(redsandConfig, redsandActorIsPlayer, redsandStaff);
		redsandReleasedSelected = allowRedsandLayers && a_layer == Redsand::Weapons::TrailLayer::Primary &&
			Redsand::Weapons::RedsandArcAllowed(redsandConfig, redsandActorIsPlayer, redsandStaff);
		redsandScopeAllowed = redsandOrdinaryAllowed || redsandReleasedSelected;
		redsandLegacyTrail = !redsandScopeAllowed;
