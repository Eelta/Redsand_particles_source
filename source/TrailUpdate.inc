if (!redsandChecked && trailParticle && trailParticle->particleObject) {
			CacheRedsandEmissionMeshes(trailParticle->particleObject.get());
			RE::BSVisit::TraverseScenegraphGeometries(trailParticle->particleObject.get(), [&](RE::BSGeometry* geometry) {
				if (geometry->name == "RedsandParticles_Drain_v1"sv) {
					if (auto system = netimmerse_cast<RE::NiParticleSystem*>(geometry)) {
						redsandSystems.emplace_back(system);
						for (auto controller = system->GetControllers(); controller; controller = controller->GetNext()) {
							if (controller->target != system) { continue; }
							std::optional<Redsand::Emission::Scalar> kind;
							const std::string_view type = controller->GetRTTI()->GetName();
							if (type == "NiPSysEmitterCtlr"sv) { kind = Redsand::Emission::Scalar::BirthRate; }
							else if (type == "NiPSysEmitterSpeedCtlr"sv) { kind = Redsand::Emission::Scalar::Speed; }
							else if (type == "NiPSysEmitterInitialRadiusCtlr"sv) { kind = Redsand::Emission::Scalar::Radius; }
							if (!kind) { continue; }
							if (auto interpController = netimmerse_cast<RE::NiInterpController*>(controller)) {
								auto original = netimmerse_cast<RE::NiFloatInterpolator*>(interpController->GetInterpolator(0));
								if (!original) { continue; }

								RE::NiPointer<RE::NiFloatInterpolator> copy(netimmerse_cast<RE::NiFloatInterpolator*>(original->Clone()));
								if (!copy || copy.get() == original) { continue; }
								copy->floatData.reset();
								const float fallback = *kind == Redsand::Emission::Scalar::BirthRate ? 1600.f :
									(*kind == Redsand::Emission::Scalar::Radius ? 10.f : -13.f);
								const bool signedSpeed = *kind == Redsand::Emission::Scalar::Speed;
								const bool valid = std::isfinite(original->floatValue) &&
									original->floatValue != -std::numeric_limits<float>::max() &&
									(signedSpeed || original->floatValue >= 0.f);
								const float rate = valid ? original->floatValue : fallback;
								copy->floatValue = Redsand::Emission::Value(rate, *kind, redsandConfig);
								interpController->SetInterpolator(copy.get(), 0);
								redsandBirthRates.push_back({ std::move(copy), rate, *kind });
							}
						}
						for (auto& modifier : system->GetParticleSystemRuntimeData().modifierList) {
							if (auto gravity = netimmerse_cast<RE::NiPSysGravityModifier*>(modifier.get())) {
								redsandGravity.push_back({ RE::NiPointer<RE::NiPSysGravityModifier>(gravity), gravity->strength, gravity->turbulence });
							}
						}
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			redsandManaged = !redsandSystems.empty();
			if (redsandManaged && redsandConfig.diagnostics) {
				[[maybe_unused]] static const bool controlsDiagnostic = [&] {
					const auto count = [&](Redsand::Emission::Scalar kind) {
						return std::count_if(redsandBirthRates.begin(), redsandBirthRates.end(),
							[&](const auto& item) { return item.kind == kind; });
					};
					RedsandLog::info("Redsand live emission controls: birth={} speed={} radius={} gravity={}",
						count(Redsand::Emission::Scalar::BirthRate), count(Redsand::Emission::Scalar::Speed),
						count(Redsand::Emission::Scalar::Radius), redsandGravity.size());
					return true;
				}();
				if (redsandEmitterMeshes.empty()) {
					[[maybe_unused]] static const bool missingEmitterDiagnostic = [] {
						RedsandLog::warn("Redsand emission: managed particle effect has no named source meshes; tip/style placement unavailable");
						return true;
					}();
				} else {
					[[maybe_unused]] static const bool emitterDiagnostic = [&] {
						const auto* first = redsandEmitterMeshes.front().geometry.get();
						const auto* type = first->GetRTTI();
						RedsandLog::info("Redsand emission: sources={} firstName={} firstRTTI={} firstAddress={}",
							redsandEmitterMeshes.size(), first->name.c_str(), type ? type->GetName() : "unknown", fmt::ptr(first));
						return true;
					}();
				}
			}
			redsandChecked = true;
		}
		if (redsandManaged) {
			redsandConfig = RedsandSettings::Get();
			auto actor = actorHandle.get();
			if (!actor || !redsandConfig.swordEnabled || !(actor->IsPlayerRef() ? redsandConfig.swordPlayer : redsandConfig.swordNPC)) {
				trailParticle->particleObject->SetAppCulled(true);
				trailParticle->age = trailParticle->lifetime + 1.f;
				bActive = false;
				return false;
			}
			for (auto& birth : redsandBirthRates) {
				birth.interpolator->floatValue = Redsand::Emission::Value(birth.originalRate, birth.kind, redsandConfig);
			}
			for (auto& gravity : redsandGravity) {
				gravity.modifier->strength = gravity.strength * redsandConfig.swordSpread;
				gravity.modifier->turbulence = gravity.turbulence * redsandConfig.swordSpread;
			}
			trailParticle->lifetime = std::max(trailParticle->lifetime, trailParticle->age + 7.f);
			if (bExpired) {
				return UpdateRedsandParticles(a_deltaTime);
			}
		}
