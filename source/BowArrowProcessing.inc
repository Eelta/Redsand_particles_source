bool Allowed(RE::Actor* actor)
		{
			const auto configuration = BowConfiguration(actor);
			const bool allowed = actor && actor->Is3DLoaded() && !actor->IsDead() && configuration.bowEnabled &&
				(actor->IsPlayerRef() ? configuration.bowPlayer : configuration.bowNPC);
			if (configuration.diagnostics && actor) {
				const unsigned scope = (configuration.bowEnabled ? 1u : 0u) | (configuration.bowPlayer ? 2u : 0u) | (configuration.bowNPC ? 4u : 0u);
				static unsigned previousScope = 8;
				static std::unordered_set<RE::FormID> reported;
				if (scope != previousScope) { reported.clear(); previousScope = scope; }
				if (reported.size() < 128 && reported.insert(actor->GetFormID()).second) {
					RedsandLog::info("Redsand bow scope: actor={:08X} player={} Enabled={} Player={} NPC={} Allowed={}",
						actor->GetFormID(), actor->IsPlayerRef(), configuration.bowEnabled, configuration.bowPlayer, configuration.bowNPC, allowed);
				}
			}
			return allowed;
		}

		void DrainArrowSamples(std::vector<ArrowSample>& samples)
		{
			pendingSamples.Drain(samples);
			batchBirths.clear();
			for (const auto& sample : samples) {
				if (sample.livingTime <= 0.25f) { batchBirths.insert(sample.handle); }
			}
		}

		bool BirthSamplePending(RE::ProjectileHandle handle)
		{
			return birthReservations.Contains(handle) || pendingSamples.HasBirth(handle) || batchBirths.contains(handle);
		}

		bool MakeShotRoom()
		{
			if (shots.size() < maxQueuedSamples) { return true; }
			std::lock_guard lock(sampleLock);
			auto victim = shots.end();
			for (auto candidate = shots.begin(); candidate != shots.end(); ++candidate) {
				if ((!candidate->second.flightEnded && candidate->second.lastLivingTime <= 0.25f) || candidate->second.rendererResident || BirthSamplePending(candidate->first)) { continue; }
				if (victim == shots.end() || candidate->second.lastSampleSequence < victim->second.lastSampleSequence) { victim = candidate; }
			}
			if (victim == shots.end()) { return false; }
			shots.erase(victim);
			return true;
		}

		void PruneSilentShots()
		{
			std::lock_guard lock(sampleLock);
			std::erase_if(shots, [](const auto& entry) {
				return entry.second.silence > Simulation::maxLifetime + 1.f && !BirthSamplePending(entry.first);
			});
		}

		void ProcessArrow(const ArrowSample& sample, float density, bool rings = true, bool trails = true)
		{
			batchBirths.erase(sample.handle);
			const auto latestSequence = sample.lastSequence ? sample.lastSequence : sample.sequence;
			const float latestLivingTime = sample.latestLivingTime >= 0.f ? sample.latestLivingTime : sample.livingTime;
			auto shot = shots.find(sample.handle);
			if (shot != shots.end()) {
				if (latestSequence <= shot->second.lastSampleSequence) { return; }
				if (sample.sequence > shot->second.lastSampleSequence && latestLivingTime <= 0.25f &&
					latestLivingTime + 0.001f < shot->second.lastLivingTime) {

					std::erase_if(streams, [&](auto& stream) {
						if (stream->charging || stream->projectile != sample.handle) { return false; }
						stream->Retire();
						return true;
					});
					shot->second = {};
				}
			}
			if (shot == shots.end()) {
				if (sample.livingTime > 0.25f || !NearPlayer(sample.from)) { return; }
				auto shooter = sample.shooter.get();
				auto* actor = shooter ? shooter->As<RE::Actor>() : nullptr;
				if (!Allowed(actor)) { return; }
				if (!MakeShotRoom()) { return; }
				shot = shots.try_emplace(sample.handle).first;
			}
			shot->second.lastSampleSequence = latestSequence;
			shot->second.lastLivingTime = latestLivingTime;
			const bool flightWasEnded = shot->second.flightEnded;
			shot->second.flightEnded = flightWasEnded || !sample.flying;
			shot->second.silence = 0.f;
			if (flightWasEnded) { return; }
			Stream* stream = nullptr;
			for (auto& candidate : streams) {
				if (!candidate->charging && candidate->projectile == sample.handle) { stream = candidate.get(); break; }
			}
			if (stream && !stream->emitting) { return; }
			if (stream) {
				const auto current = StreamConfiguration(*stream, RedsandSettings::Get());
				density = current.bowDensity; rings = current.bowRings; trails = current.bowTrails;
			}
			if (!stream) {

				const bool firstStream = !shot->second.streamCreated;
				if (!firstStream && !shot->second.allowRendererRecovery) { return; }
				if ((!firstStream && !sample.flying) || !NearPlayer(sample.from)) { return; }
				auto shooter = sample.shooter.get();
				auto* actor = shooter ? shooter->As<RE::Actor>() : nullptr;
				if (!Allowed(actor)) { return; }

				const bool released = shot->second.ConsumeRelease();
				if (released && (chargeStates.contains(actor->GetHandle()) || chargeStates.size() < 128)) {
					chargeStates[actor->GetHandle()].SignalRelease(sample.sequence);
				}
				Vec3 origin = sample.from;
				std::optional<RE::NiColorA> chargedColor;
				for (auto& charge : streams) {
					if (released && charge->charging && charge->actor == actor->GetHandle() && charge->chargeSequence <= sample.sequence) {
						if (charge->silence < 0.2f && (charge->tip - origin).Length() < 160.f) {
							origin = charge->tip;
							chargedColor = charge->color;
						}

						charge->Stop();
					}
				}
				if (!rings && !trails) {
					shot->second.streamCreated = true;
					shot->second.allowRendererRecovery = true;
					return;
				}
				stream = CreateStream(actor, RE::TESForm::LookupByID<RE::TESObjectCELL>(sample.cellID), origin, sample.handle.native_handle(), sample.weaponID);
				if (!stream) { return; }
				shot->second.streamCreated = true;
				shot->second.allowRendererRecovery = false;
				shot->second.rendererResident = true;
				if (chargedColor) { stream->color = *chargedColor; }
				stream->projectile = sample.handle;
				if (firstStream && rings && sample.livingTime <= 0.25f) { stream->simulation.Ring(origin, sample.direction, StreamDensity(*stream, density), sample.power); }
			}
			if (trails) {
				if (sample.path.Size()) { stream->simulation.Trail(sample.path, StreamDensity(*stream, density)); }
				else { stream->simulation.Trail(sample.from, sample.to, sample.delta, StreamDensity(*stream, density), sample.continuous); }
			}
			stream->silence = 0.f;
			if (!sample.flying) { stream->Stop(); }
		}

		void RecoverableStreamLoss(const Stream& stream)
		{
			Redsand::Bow::ForgetRendererLease(shots, stream);

			if (!stream.charging && stream.emitting) {
				if (auto shot = shots.find(stream.projectile); shot != shots.end()) { shot->second.allowRendererRecovery = true; }
			}
		}

		template <class ActorForStream>
		void ApplyStreamConfiguration(const RedsandSettings::Config& configuration, bool display, ActorForStream actorForStream)
		{
			std::erase_if(streams, [&](auto& stream) {
				auto actor = actorForStream(*stream);
				const auto current = StreamConfiguration(*stream, configuration);
				const bool inScope = actor && (actor->IsPlayerRef() ? current.bowPlayer : current.bowNPC);
				if (!display || !current.bowEnabled || !inScope || (stream->charging && !current.bowGathering) ||
					(!stream->charging && !current.bowRings && !current.bowTrails)) {
					RecoverableStreamLoss(*stream);
					stream->Retire();
					return true;
				}
				stream->simulation.SetEnabledKinds(current.bowGathering, current.bowRings, current.bowTrails);
				return false;
			});
		}

		bool StreamFinished(const Stream& stream)
		{

			return stream.simulation.Empty() && (!stream.emitting ||
				(!stream.charging && stream.silence > Simulation::maxLifetime));
		}
