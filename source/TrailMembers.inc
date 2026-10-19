bool UpdateRedsandParticles(float a_deltaTime);
	void RetireRedsandNativeTrail();
	bool redsandNativeOnly = false;
	bool redsandParticleSelected = false;
	float redsandNativePendingTime = 0.f;
	void UpdateRedsandEmission();
	void CacheRedsandEmissionMeshes(RE::NiAVObject* root);
	void LogRedsandParticles(RE::BSGeometry* geometry, std::string_view stage);
	float redsandNextDiagnostic = 0.f;
	RedsandSettings::Config redsandConfig;
	RE::NiColorA redsandInheritedColor{ .025f, .080f, .650f, 1.f };
	Redsand::Weapons::Profile redsandWeaponProfile = Redsand::Weapons::Profile::Default;
	struct RedsandBirthRate {
		RE::NiPointer<RE::NiFloatInterpolator> interpolator;
		float originalRate;
		Redsand::Emission::Scalar kind;
	};
	struct RedsandGravity {
		RE::NiPointer<RE::NiPSysGravityModifier> modifier;
		float strength, turbulence;
	};
	struct RedsandEmitterMesh {
		RE::NiPointer<RE::NiAVObject> geometry;
		RE::NiTransform originalLocal;
	};
	std::vector<RedsandBirthRate> redsandBirthRates;
	std::vector<RedsandGravity> redsandGravity;
	std::vector<RedsandEmitterMesh> redsandEmitterMeshes;
	RE::NiPoint3 redsandPreviousTip;
	bool redsandTipSample = false;
	bool redsandChecked = false;
	bool redsandManaged = false;
	bool redsandLegacyTrail = false;
	bool redsandActorIsPlayer = false;
	bool redsandScopeAllowed = false;
	RedsandDrainState redsandDrain;
	std::vector<RE::NiPointer<RE::NiParticleSystem>> redsandSystems;
