	bool UpdateRedsandParticles(float a_deltaTime);
	bool redsandChecked = false;
	bool redsandManaged = false;
	bool redsandLegacyTrail = false;
	RedsandDrainState redsandDrain;
	std::vector<RE::NiPointer<RE::NiParticleSystem>> redsandSystems;
