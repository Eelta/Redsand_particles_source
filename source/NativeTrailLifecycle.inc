AttackTrail::~AttackTrail()
{
	if (redsandNativeOnly) { RetireRedsandNativeTrail(); }
}

bool AttackTrail::ShouldAddPrecisionTrail() const
{
	return !redsandReleasedSelected && !redsandNativeOnly && redsandParticleSelected && redsandScopeAllowed &&
		redsandConfig.swordKeepPrecisionTrail && trailParticle;
}

void AttackTrail::RetireRedsandNativeTrail()
{
	if (trailParticle) {
		if (trailParticle->particleObject) { trailParticle->particleObject->SetAppCulled(true); }
		trailParticle->age = trailParticle->lifetime + 1.f;
	}
	bActive = false;
}
