							auto* handler = PrecisionHandler::GetSingleton();
							const bool trueLength = Settings::bTrailUseAttackCollisionLength || a_collisionDefinition.bTrailUseTrueLength;
							auto redsandTrail = std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, trueLength, a_collisionDefinition.trailOverride);
							using Redsand::Weapons::TrailLayer;
							const auto companion = redsandTrail->ShouldAddPrecisionTrail() ? TrailLayer::Precision :
								redsandTrail->ShouldAddOrdinaryParticles() ? TrailLayer::OrdinaryParticles : TrailLayer::Primary;
							handler->_attackTrails.emplace_back(std::move(redsandTrail));
							if (companion != TrailLayer::Primary) {
								handler->_attackTrails.emplace_back(std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, trueLength, a_collisionDefinition.trailOverride, companion));
							}
