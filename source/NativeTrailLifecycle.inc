AttackTrail::~AttackTrail()
{
	if (redsandNativeOnly) { RetireRedsandNativeTrail(); }
	if (redsandReleasedSelected) { RetireRedsandSwingTrail(); }
}

bool AttackTrail::ShouldAddPrecisionTrail() const
{
	return redsandOrdinaryAllowed && !redsandConfig.swordReleasedTrail && redsandParticleSelected && trailParticle;
}

bool AttackTrail::ShouldAddOrdinaryParticles() const
{
	return redsandOrdinaryAllowed && (redsandReleasedSelected || (!redsandParticleSelected && trailParticle));
}

void AttackTrail::RetireRedsandNativeTrail()
{
	if (trailParticle) {
		if (trailParticle->particleObject) { trailParticle->particleObject->SetAppCulled(true); }
		trailParticle->age = trailParticle->lifetime + 1.f;
	}
	bActive = false;
}
