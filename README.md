# Redsand_particles

Glowing weapon-trail particles with independent fading.

## Install

Install `Redsand_particles.zip` once with your mod manager. Requires Precision and its SKSE and Address Library dependencies.

The package includes the modified DLL, particle assets and plugin. SE and AE are enabled; VR is disabled. Runtime checks follow upstream Precision.

## Build

Requires Windows x64, Git, Visual Studio 2022 with C++ tools, Windows SDK, CMake, 7-Zip and internet access.

Run `BUILD.cmd` to fetch the latest Precision source, apply changes, build and test the DLL, and create `output/Redsand_particles.zip`.

Successful builds remove `.build`; failed builds retain it for inspection. `BUILD.json` records source commits and the DLL checksum.

## Customize

- `source/`: particle lifecycle logic.
- `assets/`: models, textures, enchantment mapping and settings.
- `build.json`: repositories, revisions and build parallelism.

Defaults: 1600 particles/second, capacity 8192, lifetime 2.8 seconds with 0.5-second variation, brightness multiplier 3.6.

Adjust NIF emission rate for density, lifetime for trail persistence, and the color modifier for fading. Normal particle fading does not use `fTrailFadeOutTime`. Lifetimes beyond five seconds require adjusting the source safety timeout.

## Licenses

Modified Precision code remains subject to GPL-3.0 and the retained upstream exceptions. Third-party components and assets retain their respective licenses, included in `licenses/` and in the generated package. Kenney particle textures use CC0.

When distributing a DLL, provide its corresponding source and build scripts. A script that fetches the latest upstream HEAD alone does not preserve the source of a released build.
