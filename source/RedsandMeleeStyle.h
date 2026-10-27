#pragma once

#include "RedsandWeaponProfiles.h"
#include "RedsandSettings.h"
#include <optional>

namespace Redsand::Weapons
{
	constexpr bool TrailDefinitionAllowed(bool legacy, bool particleDefinition)
	{
		return legacy ? !particleDefinition : particleDefinition;
	}
	constexpr bool CheckMagicConditions(bool legacy, bool hasCostActorValue, bool = false)
	{
		return hasCostActorValue || !legacy;
	}
	constexpr bool UseEnchantmentFallback(bool legacy, bool companion, bool resolvedColor)
	{
		return !legacy && !companion && !resolvedColor;
	}

	struct EnchantmentTint { float red, green, blue; };
	constexpr bool HasEnchantmentHue(EnchantmentTint color)
	{
		const float high = std::max({ color.red, color.green, color.blue });
		const float low = std::min({ color.red, color.green, color.blue });

		return high > .025f && high - low >= .025f && high - low >= high * .12f;
	}
	template <class Colors>
	std::optional<EnchantmentTint> FirstEnchantmentHue(const Colors& colors)
	{
		for (const auto color : colors) {
			const EnchantmentTint candidate{ color.red / 255.f, color.green / 255.f, color.blue / 255.f };
			if (HasEnchantmentHue(candidate)) { return candidate; }
		}
		return std::nullopt;
	}
	template <class Magic, class Resolve>
	std::optional<EnchantmentTint> ResolveEnchantmentTint(Magic* magic, Resolve&& resolve)
	{
		if (!magic) { return std::nullopt; }


		auto* dominant = magic->GetCostliestEffectItem();
		if (dominant) { if (auto tint = resolve(dominant)) { return tint; } }
		for (auto* item : magic->effects) {
			if (item && item != dominant) { if (auto tint = resolve(item)) { return tint; } }
		}
		return std::nullopt;
	}
	inline constexpr EnchantmentTint defaultTrailTint{ .025f, .080f, .650f };
	enum class EnchantmentTone { Fire, Frost, Shock, Poison };
	constexpr EnchantmentTint TintFor(EnchantmentTone tone)
	{
		switch (tone) {
		case EnchantmentTone::Fire: return { 1.f, .30f, .055f };
		case EnchantmentTone::Frost: return { .34f, .76f, 1.f };
		case EnchantmentTone::Shock: return { .48f, .58f, 1.f };
		default: return { .32f, 1.f, .12f };
		}
	}
	constexpr bool EnchantmentHasCharge(bool poison, bool knownCharge, double charge)
	{
		return poison || !knownCharge || charge > 0.;
	}
	constexpr bool InsufficientEnchantmentCharge(bool poison, bool hasEnchantment, bool knownPercent, double percent,
		bool costAvailable, float chargeValue, float cost)
	{
		return !poison && hasEnchantment && ((knownPercent && percent <= 0.) ||
			(costAvailable && cost > 0.f && chargeValue < cost));
	}
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
		for (const auto prefix : std::array<std::string_view, 3>{ "RedsandParticles/", "Effects/WeaponTrails/ElementalDesert/", "RedsandSwingTrail/" }) {
			if (path.size() >= prefix.size() && detail::EqualMeshPath(path.substr(0, prefix.size()), prefix)) { return true; }
		}
		return false;
	}

	constexpr bool IsLEDWeaponMesh(std::string_view path)
	{
		for (const auto prefix : std::array<std::string_view, 2>{ "Data/Meshes/", "Meshes/" }) {
			if (path.size() >= prefix.size() && detail::EqualMeshPath(path.substr(0, prefix.size()), prefix)) { path.remove_prefix(prefix.size()); break; }
		}
		if (detail::EqualMeshPath(path, "DawnbreakerRework/Dawnbreaker/AttackTrailMagic_DawnBreaker.nif")) { return true; }
		constexpr std::string_view prefix = "WeaponTrails/";
		if (path.size() <= prefix.size() || !detail::EqualMeshPath(path.substr(0, prefix.size()), prefix)) { return false; }
		path.remove_prefix(prefix.size());
		for (const auto name : std::array<std::string_view, 21>{
			"AttackTrail.nif", "AttackTrail_SN.nif", "AttackTrailD.nif", "AttackTrailMagic_Blue.nif", "AttackTrailMagic_Blue_S.nif",
			"AttackTrailMagic_DawnBreaker.nif", "AttackTrailMagic_FireSword.nif", "AttackTrailMagic_FireSword_SF.nif",
			"AttackTrailMagic_FrostSword.nif", "AttackTrailMagic_FrostSword_SI.nif", "AttackTrailMagic_Furry.nif",
			"AttackTrailMagic_MehrunesRazor.nif", "AttackTrailMagic_MolagBal.nif", "AttackTrailMagic_poison.nif", "AttackTrailMagic_poison_S.nif",
			"AttackTrailMagic_Purple.nif", "AttackTrailMagic_Purple_S.nif", "AttackTrailMagic_Red.nif", "AttackTrailMagic_Red_S.nif",
			"AttackTrailMagic_ShockSword.nif", "AttackTrailMagic_ShockSword_SS.nif" }) {
			if (detail::EqualMeshPath(path, name)) { return true; }
		}
		return false;
	}

	inline std::string ResolveMeleeMesh(bool particlesAllowed, std::string_view requested, std::string_view installedRibbon,
		bool requestedAvailable = true)
	{


		if (IsLEDWeaponMesh(requested)) {
			if (particlesAllowed) { return "RedsandParticles/Default.nif"; }
			if (requestedAvailable) { return std::string(requested); }
			requested = "RedsandParticles/Default.nif";
		}
		if (particlesAllowed || !IsParticleMesh(requested)) { return std::string(requested); }
		return std::string(installedRibbon.empty() || IsParticleMesh(installedRibbon) || IsLEDWeaponMesh(installedRibbon) ?
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
