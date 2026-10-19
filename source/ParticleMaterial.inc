const bool uniqueParticleMaterial = redsandNativeOnly || (!redsandLegacyTrail && a_geometry->name == "RedsandParticles_Drain_v1"sv);
					LogRedsandParticles(a_geometry, "material-source"sv);
					effectShader->SetMaterial(newMaterial, uniqueParticleMaterial);
					redsandMaterialCopied = true;
					LogRedsandParticles(a_geometry, "material-private"sv);
					if (uniqueParticleMaterial && redsandConfig.diagnostics) {
						static std::atomic<std::uint32_t> loggedMaterials{ 0 };
						const auto bit = 1u << static_cast<unsigned>(redsandWeaponProfile);
						if (!(loggedMaterials.fetch_or(bit) & bit)) {
							const auto folder = Redsand::Weapons::ProfileFolder(redsandWeaponProfile);
							RedsandLog::info("Redsand melee loaded material: profile={} path={} texture={} greyscale={}",
								folder.empty() ? "Default"sv : folder, newMaterial->sourceTexturePath.c_str(),
								newMaterial->sourceTexture ? newMaterial->sourceTexture->name.c_str() : "unavailable",
								newMaterial->greyscaleTexturePath.c_str());
						}
					}
