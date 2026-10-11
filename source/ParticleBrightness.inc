if (!redsandLegacyTrail && a_geometry->name == "RedsandParticles_Drain_v1"sv) {
						const auto& color = effectShaderMaterial->baseColor;
						const float peak = std::max({ color.red, color.green, color.blue, 0.f });
						effectShaderMaterial->baseColorScale = peak > 0.f ? 2.6f / peak : 0.f;
					}
