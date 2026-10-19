struct RendererDiagnostics
		{
			std::uint64_t expired = 0, detached = 0, unavailable = 0, timeout = 0, recovered = 0;
		};
		RendererDiagnostics rendererDiagnostics;

		template <class Clock>
		bool RendererDiagnosticDue(bool diagnostics, typename Clock::time_point& next)
		{
			if (!diagnostics) { return false; }
			const auto now = Clock::now();
			if (now < next) { return false; }
			next = now + std::chrono::seconds(1);
			return true;
		}

		bool ConfigureRendererMaterial(Stream& stream, RE::NiParticleSystem* system, float brightness)
		{
			auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(system->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = shader ? skyrim_cast<RE::BSEffectShaderMaterial*>(shader->material) : nullptr;
			if (!material) { return false; }
			auto* copy = static_cast<RE::BSEffectShaderMaterial*>(material->Create());
			if (!copy) { return false; }
			copy->CopyMembers(material);
			const auto config = RedsandSettings::Get();
			const auto& custom = config.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Bow)];
			const RE::NiColorA color = custom.custom ? RE::NiColorA{ custom.rgb[0], custom.rgb[1], custom.rgb[2], stream.color.alpha } : stream.color;
			copy->baseColor = color;
			const float peak = std::max({ color.red, color.green, color.blue, 0.f });
			copy->baseColorScale = peak > 0.f ? 2.6f * 5.4f * brightness / peak : 0.f;
			shader->SetMaterial(copy, true);
			copy->~BSEffectShaderMaterial();
			RE::free(copy);
			stream.rendererBrightness = brightness;
			stream.rendererColor = color;
			return true;
		}

		void ReportRendererMaterial(const Stream& stream, RE::NiParticleSystem* system)
		{
			const auto style = stream.simulation.Style();
			const unsigned profile = style == Redsand::Bow::TrailStyle::VanillaBow ? 1u : style == Redsand::Bow::TrailStyle::VanillaCrossbow ? 2u : 0u;
			static bool reported[3]{};
			if (reported[profile]) { return; }
			auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(system->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = shader ? skyrim_cast<RE::BSEffectShaderMaterial*>(shader->material) : nullptr;
			if (!material) { return; }
			reported[profile] = true;
			RedsandLog::info("Redsand bow material: profile={} mesh={} materialTexture={} loadedTexture={}",
				profile == 1 ? "Bow" : profile == 2 ? "Crossbow" : "Default", stream.meshPath,
				material->sourceTexturePath.c_str(), material->sourceTexture ? material->sourceTexture->name.c_str() : "unavailable");
		}

		bool InitializeRenderer(Stream& stream, float brightness)
		{
			auto* root = stream.effect->particleObject.get();
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
				auto* system = netimmerse_cast<RE::NiParticleSystem*>(geometry);
				if (stream.system || !system || geometry->name != "RedsandParticles_Drain_v1"sv) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* data = system->GetParticlesRuntimeData().particleData.get();
				if (!data) { return RE::BSVisit::BSVisitControl::kContinue; }
				auto& vertices = data->GetParticlesRuntimeData();
				if (!vertices.positions || !vertices.color || !vertices.radii || !vertices.sizes || !vertices.maxNumVertices) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (!ConfigureRendererMaterial(stream, system, brightness)) { return RE::BSVisit::BSVisitControl::kContinue; }

				auto& runtime = system->GetParticleSystemRuntimeData();
				for (auto& modifier : runtime.modifierList) { if (modifier) { modifier->SetActive(false); } }
				runtime.isWorldspace = false;
				runtime.dynamicBounds = false;
				runtime.resetSystem = false;
				vertices.numVertices = 0;
				stream.system.reset(system);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!stream.system) { return false; }
			stream.rendererRoot = root;

			DisableControllers(root);
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
				geometry->SetAppCulled(geometry != stream.system.get());
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return true;
		}

		enum class RendererStatus { Pending, Ready, Failed };

		bool SubmitRendererVisibility(Stream& stream, std::uint16_t count)
		{
			auto* system = stream.system.get();
			if (!system || !stream.effect || stream.effect->age > stream.effect->lifetime) { return false; }
			auto* root = stream.effect->particleObject.get();
			if (!root || root == system || root != stream.rendererRoot) { return false; }
			unsigned depth = 0;
			for (auto* object = static_cast<RE::NiAVObject*>(system); object && depth < 32; object = object->parent, ++depth) {
				if (object == root) {
					system->SetAppCulled(count == 0);
					if (count) { root->SetAppCulled(false); }
					return true;
				}
			}
			return false;
		}

		RendererStatus PrepareRenderer(Stream& stream, float brightness = 1.f, bool diagnostics = false)
		{
			if (!stream.effect || stream.effect->age > stream.effect->lifetime) {
				if (diagnostics) { ++rendererDiagnostics.expired; }
				return RendererStatus::Failed;
			}

			stream.effect->lifetime = std::max(stream.effect->lifetime, stream.effect->age + 7.f);
			auto* root = stream.effect->particleObject.get();
			if (stream.system && root != stream.rendererRoot) {

				stream.system.reset();
				stream.rendererRoot = nullptr;
				stream.rendererStartAge = stream.age;
				stream.rendererWasAttached = false;
				stream.rendererResourcesPending = false;
			}
			if (root && stream.rendererWasAttached && !root->parent) {
				if (diagnostics) { ++rendererDiagnostics.detached; }
				return RendererStatus::Failed;
			}
			if (root && root->parent) { stream.rendererWasAttached = true; }
			if (root && (stream.system || InitializeRenderer(stream, brightness))) {
				auto* system = stream.system.get();
				auto* data = system->GetParticlesRuntimeData().particleData.get();
				const bool usable = data && data->GetParticlesRuntimeData().positions && data->GetParticlesRuntimeData().color &&
					data->GetParticlesRuntimeData().radii && data->GetParticlesRuntimeData().sizes && data->GetParticlesRuntimeData().maxNumVertices &&
					std::isfinite(system->world.scale) && system->world.scale > 0.0001f;
				if (usable) {
					if (stream.rendererResourcesPending && diagnostics) { ++rendererDiagnostics.recovered; }
					stream.rendererResourcesPending = false;
					const auto config = RedsandSettings::Get();
					const auto& custom = config.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Bow)];
					const RE::NiColorA wanted = custom.custom ? RE::NiColorA{ custom.rgb[0], custom.rgb[1], custom.rgb[2], stream.color.alpha } : stream.color;
					if (stream.rendererBrightness != brightness || stream.rendererColor.red != wanted.red || stream.rendererColor.green != wanted.green || stream.rendererColor.blue != wanted.blue) { ConfigureRendererMaterial(stream, system, brightness); }
					if (diagnostics) { ReportRendererMaterial(stream, system); }
					return RendererStatus::Ready;
				}
				if (!stream.rendererResourcesPending) {
					stream.rendererResourcesPending = true;
					stream.rendererStartAge = stream.age;
					if (diagnostics) { ++rendererDiagnostics.unavailable; }
				}
			}

			if (stream.age - stream.rendererStartAge < 2.f) { return RendererStatus::Pending; }
			if (diagnostics) { ++rendererDiagnostics.timeout; }
			RedsandLog::warn("Redsand bow particles: renderer initialization timed out for one stream, mesh={}", particleMesh);
			return RendererStatus::Failed;
		}
