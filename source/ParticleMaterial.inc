					// Drain alpha changes per trail; never reuse a cached material for these particles.
					const bool uniqueParticleMaterial = !redsandLegacyTrail && a_geometry->name == "RedsandParticles_Drain_v1"sv;
					effectShader->SetMaterial(newMaterial, uniqueParticleMaterial);
