#pragma once

#include "RedsandWeaponProfiles.h"
#include "RedsandSettings.h"

namespace Redsand::Weapons
{
	constexpr bool MeleeScopeAllowed(const RedsandSettings::Config& config, bool player, bool staff)
	{
		return !staff && config.swordEnabled && (player ? config.swordPlayer : config.swordNPC);
	}

	constexpr bool IsParticleMesh(std::string_view path)
	{
		for (const auto prefix : std::array<std::string_view, 2>{ "Data/Meshes/", "Meshes/" }) {
			if (path.size() >= prefix.size() && detail::EqualMeshPath(path.substr(0, prefix.size()), prefix)) {
				path.remove_prefix(prefix.size());
				break;
			}
		}
		for (const auto prefix : std::array<std::string_view, 2>{ "RedsandParticles/", "Effects/WeaponTrails/ElementalDesert/" }) {
			if (path.size() >= prefix.size() && detail::EqualMeshPath(path.substr(0, prefix.size()), prefix)) { return true; }
		}
		return false;
	}

	inline std::string ResolveMeleeMesh(bool particlesAllowed, std::string_view requested, std::string_view installedRibbon)
	{
		if (particlesAllowed || !IsParticleMesh(requested)) { return std::string(requested); }
		return std::string(installedRibbon.empty() || IsParticleMesh(installedRibbon) ?
			"Effects/WeaponTrails/AttackTrail.nif" : installedRibbon);
	}

	struct EmissionSpan { float start, end; };

	constexpr EmissionSpan MeleeEmissionSpan(Profile profile)
	{
		switch (profile) {
		case Profile::Dagger: return { 0.12f, 0.94f };
		case Profile::Sword: return { 0.06f, 1.00f };
		case Profile::WarAxe: return { 0.68f, 1.00f };
		case Profile::Mace: return { 0.82f, 1.00f };
		case Profile::Greatsword: return { 0.04f, 1.00f };
		case Profile::Battleaxe: return { 0.64f, 1.00f };
		case Profile::Warhammer: return { 0.80f, 1.00f };
		default: return { 0.00f, 1.00f };
		}
	}
}
