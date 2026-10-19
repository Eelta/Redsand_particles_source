
		void UpdateSlashSpeed(RE::BSGeometry* geometry, const RedsandSettings::Config& config)
		{
			auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			if (!shader || shader->name != "RSRGBv1_3"sv) { return; }


            static const RE::BSFixedString distanceApplied{ "RSSlashDistanceApplied_v1" }, spreadApplied{ "RSSlashSpreadApplied_v1" };
            auto* distance = netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(distanceApplied));
            auto* spread = netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(spreadApplied));
            if (distance && spread && distance->value == config.slashDistance && spread->value == config.slashSpread) { return; }
            unsigned updated = 0;
			unsigned examined = 0;
			for (auto controller = geometry->GetControllers(); controller && examined++ < 24; controller = controller->GetNext()) {
				if (controller->target != geometry) { continue; }
				const std::string_view type{ controller->GetRTTI()->GetName() };
				const bool speed = type == "NiPSysEmitterSpeedCtlr";
				const bool declination = type == "NiPSysEmitterDeclinationVarCtlr";
				if (!speed && !declination && type != "NiPSysEmitterPlanarAngleVarCtlr") { continue; }
				static const RE::BSFixedString speedName{ "RSSlashSpeed_v2" }, declinationName{ "RSSlashDeclination_v1" }, planarName{ "RSSlashPlanar_v1" };
				const auto& name = speed ? speedName : declination ? declinationName : planarName;
				auto* control = netimmerse_cast<RE::NiInterpController*>(controller);
				auto* original = control ? netimmerse_cast<RE::NiFloatInterpolator*>(control->GetInterpolator(0)) : nullptr;
				if (!original) { return; }
				auto* marker = netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(name));
				if (!marker) {
					if (original->floatData || !std::isfinite(original->floatValue) || original->floatValue <= 0.f) { return; }
					RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(name, -original->floatValue) };
					if (!extra || !geometry->AddExtraData(extra.get())) { return; }
					marker = extra.get();
				}
				if (marker->value < 0.f) {
					RE::NiPointer<RE::NiFloatInterpolator> copy{ netimmerse_cast<RE::NiFloatInterpolator*>(original->Clone()) };
					if (!copy || copy.get() == original) { return; }
					copy->floatData.reset();
					control->SetInterpolator(copy.get(), 0);
					original = copy.get();
					marker->value = -marker->value;
					static std::atomic<unsigned> travelLogs{ 0 };
					if (travelLogs.load(std::memory_order_relaxed) < 36 && travelLogs.fetch_add(1, std::memory_order_relaxed) < 36) { logger::info("Redsand slash emission: node={} control={} base={} distance={} spread={}",
						geometry->name.c_str(), type, marker->value, config.slashDistance, config.slashSpread); }
				}
				original->floatValue = speed ? SlashSpeed(marker->value, config) : SlashSpread(marker->value, config);
                ++updated;
			}


            if (updated == 3) {
                const auto remember = [&](const RE::BSFixedString& name, RE::NiFloatExtraData* marker, float value) {
                    if (marker) { marker->value = value; }
                    else {
                        RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(name, value) };
                        if (extra) { geometry->AddExtraData(extra.get()); }
                    }
                };
                remember(distanceApplied, distance, config.slashDistance);
                remember(spreadApplied, spread, config.slashSpread);
            }
		}
