if (!redsandLegacyTrail && a_geometry->name == "RedsandParticles_Drain_v1"sv) {
				if (a_init && !redsandMaterialCopied) { LogRedsandParticles(a_geometry, "material-copy-failed"sv); }
				const auto& color = effectShaderMaterial->baseColor;
				const float peak = std::max({ color.red, color.green, color.blue, 0.f });
				if (!a_init) { LogRedsandParticles(a_geometry, "frame"sv); }
				effectShaderMaterial->baseColorScale = peak > 0.f ? 2.6f * 5.4f * redsandConfig.swordBrightness / peak : 0.f;
				if (a_init) { LogRedsandParticles(a_geometry, "initialized"sv); }
			}
