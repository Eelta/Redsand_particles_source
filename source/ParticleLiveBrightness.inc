if (!redsandLegacyTrail && a_geometry->name == "RedsandParticles_Drain_v1"sv) {
				if (a_init && !redsandMaterialCopied) { LogRedsandParticles(a_geometry, "material-copy-failed"sv); }
				if (a_init && redsandMaterialCopied) {

					redsandInheritedColor = baseColorOverride.value_or(redsandInheritedColor);
				}
				if (!a_init || redsandMaterialCopied) {
					const auto& custom = redsandConfig.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Melee)];
					const std::array<float, 3> rgb = custom.custom ? custom.rgb :
						std::array<float, 3>{ redsandInheritedColor.red, redsandInheritedColor.green, redsandInheritedColor.blue };
					const float peak = std::max({ rgb[0], rgb[1], rgb[2], 0.f });
					const float glow = peak > 0.f ? 2.6f * 5.4f * redsandConfig.swordBrightness / peak : 0.f;
					RedsandMaterialColor::Commit(effectShader, effectShaderMaterial, rgb, glow, a_init,
						[](auto* copy) { copy->~BSEffectShaderMaterial(); RE::free(copy); });
				}
				if (!a_init) { LogRedsandParticles(a_geometry, "frame"sv); }
				if (a_init) { LogRedsandParticles(a_geometry, "initialized"sv); }
			}
