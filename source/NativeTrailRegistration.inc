							auto* handler = PrecisionHandler::GetSingleton();
							const bool trueLength = Settings::bTrailUseAttackCollisionLength || a_collisionDefinition.bTrailUseTrueLength;
							auto redsandTrail = std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, trueLength, a_collisionDefinition.trailOverride);
							const bool addPrecisionTrail = redsandTrail->ShouldAddPrecisionTrail();
							handler->_attackTrails.emplace_back(std::move(redsandTrail));
							if (addPrecisionTrail) {
								handler->_attackTrails.emplace_back(std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, trueLength, a_collisionDefinition.trailOverride, true));
							}
