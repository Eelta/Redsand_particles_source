redsandConfig = RedsandSettings::Get();
		auto* redsandWeapon = a_weaponItem && a_weaponItem->object ? a_weaponItem->object->As<RE::TESObjectWEAP>() : nullptr;
		redsandWeaponProfile = Redsand::Weapons::ForWeapon(redsandWeapon);
		const bool redsandStaff = redsandWeapon && redsandWeapon->GetWeaponType() == RE::WEAPON_TYPE::kStaff;
		const auto redsandActor = a_actorHandle.get();
		redsandActorIsPlayer = redsandActor && redsandActor->IsPlayerRef();
		redsandScopeAllowed = !redsandNativeOnly && redsandActor && Redsand::Weapons::MeleeScopeAllowed(redsandConfig, redsandActorIsPlayer, redsandStaff);
		redsandLegacyTrail = !redsandScopeAllowed;
