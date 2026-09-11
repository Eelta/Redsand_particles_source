# Redsand_particles

Glowing weapon-trail and impact particles with independent fading.

## Install

Install `Redsand_particles.zip` once with your mod manager. Requires Precision, Core Impact Framework, and their SKSE and Address Library dependencies.

The package includes the modified DLL, particle assets, impact mapping and plugin. SE and AE are enabled; VR is disabled. Runtime checks follow upstream Precision.

Optional ESPs load after `PrecisionParticleMatch.esp`: choose at most one player/NPC scope patch and one 75/50/25/5 percent density patch. With neither selected, all actors use 100% density. Install the updated DLL and all assets together; see `assets/README_ParticlePatches.txt` for details. All ESP authors are Epsilona.

Trail styles are selected per actor: no scope patch means particles for everyone; NPC-only means player ribbons and NPC particles; player-only means player particles and NPC ribbons. Keep the supplied Precision + Precision Trail mods installed in that order, with this mod last and the original `Precision_base.toml` present. Ribbons retain their original colors, brightness and lifetime; density patches apply only to particles.

Use only the rebuilt `Precision.dll` for both styles. The supplied original DLL is 2.0.4; this project's upstream source is pinned to 2.0.6. The original ribbon pipeline is retained alongside the particle extensions; do not rename/load a second Precision DLL. Static upstream comparison found only the shader-property runtime-access adaptation in `AttackTrail.cpp`, with no changes to `AttackTrail.h` or `Settings.h` between these versions.

## Build

Requires Windows x64, Git, Visual Studio 2022 with C++ tools, Windows SDK, CMake, 7-Zip and internet access.

Run `BUILD.cmd` to fetch the pinned Precision source, apply changes, build and test the DLL, and create `output/Redsand_particles.zip`.

Successful builds remove `.build`; failed builds retain it for inspection. `BUILD.json` records source commits and the DLL checksum.

## Customize

- `source/`: particle lifecycle logic.
- `assets/`: models, textures, enchantment mapping and settings.
- `build.json`: repositories, revisions and build parallelism.

Defaults: 1600 particles/second, capacity 8192, lifetime 2.8 seconds with 0.5-second variation, brightness multiplier 5.4. Particle colors use a common emissive peak (2.6 before the trail multiplier, 7.02 for impacts), replacing the previous weighted-luminance compensation that overboosted dark blue relative to enchantments. Tagged trail materials are normalized after color overrides; density patches and original ribbons are unaffected. Final perceived brightness still depends on the game's tone mapping and bloom.

Adjust NIF emission rate for density, lifetime for trail persistence, and the color modifier for fading. Normal particle fading does not use `fTrailFadeOutTime`. Lifetimes beyond five seconds require adjusting the source safety timeout.

## Licenses

Modified Precision code remains subject to GPL-3.0 and the retained upstream exceptions. Third-party components and assets retain their respective licenses, included in `licenses/` and in the generated package. Kenney particle textures use CC0.

When distributing a DLL, provide its corresponding source and build scripts. A script that fetches the latest upstream HEAD alone does not preserve the source of a released build.
