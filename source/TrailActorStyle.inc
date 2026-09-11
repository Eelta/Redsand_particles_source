		// Actors excluded by the particle scope use the original Precision ribbon trails.
		static auto* scope = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSPerk>(0x80A, "PrecisionParticleMatch.esp");
		if (auto actor = a_actorHandle.get(); actor && scope) {
			redsandLegacyTrail = !scope->perkConditions.IsTrue(actor.get(), nullptr);
		}
