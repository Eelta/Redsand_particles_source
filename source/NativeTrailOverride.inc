		if (redsandNativeOnly && a_trailOverride && a_trailOverride->meshOverride &&
			Redsand::Weapons::IsParticleMesh(*a_trailOverride->meshOverride)) {

			a_trailOverride->meshOverride.reset();
			if (!a_trailOverride->lifetimeMult && !a_trailOverride->baseColorOverride && !a_trailOverride->baseColorScaleMult) {
				a_trailOverride.reset();
			}
		}
