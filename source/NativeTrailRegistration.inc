							auto redsandTrail = std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, Settings::bTrailUseAttackCollisionLength ? true : a_collisionDefinition.bTrailUseTrueLength, a_collisionDefinition.trailOverride);
							const bool addPrecisionTrail = redsandTrail->ShouldAddPrecisionTrail();
							PrecisionHandler::GetSingleton()->_attackTrails.emplace_back(std::move(redsandTrail));
							if (addPrecisionTrail) {
								PrecisionHandler::GetSingleton()->_attackTrails.emplace_back(std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, Settings::bTrailUseAttackCollisionLength ? true : a_collisionDefinition.bTrailUseTrueLength, a_collisionDefinition.trailOverride, true));
							}
