#include "../source/RedsandSettings.h"
#include "../source/RedsandMaterialColor.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <string_view>

using namespace std::literals;

struct Color { float red, green, blue, alpha; };
struct BSEffectShaderMaterial {
	Color baseColor; float baseColorScale;
	BSEffectShaderMaterial* Create() { return new (std::malloc(sizeof(BSEffectShaderMaterial))) BSEffectShaderMaterial{}; }
	void CopyMembers(BSEffectShaderMaterial* other) { *this = *other; }
};
using Material = BSEffectShaderMaterial;
namespace RE { void free(void* p) { std::free(p); } }
struct Shader {
	Material* material;
	void SetMaterial(Material* copy, bool) { *material = *copy; }
	Material* GetMaterial() { return material; }
};
struct Geometry { std::string_view name = "RedsandParticles_Drain_v1"; };

struct Trail
{
	bool redsandLegacyTrail = false;
	RedsandSettings::Config redsandConfig;
	Color redsandInheritedColor{ .025f, .080f, .650f, 1.f };
	std::optional<Color> baseColorOverride = redsandInheritedColor;
	void LogRedsandParticles(Geometry*, std::string_view) {}
	void Update(Geometry* a_geometry, Material* effectShaderMaterial, bool a_init = false)
	{
		const bool redsandMaterialCopied = true;
		Shader shader{ effectShaderMaterial };
		auto* effectShader = &shader;
#include "../source/ParticleLiveBrightness.inc"
	}
};

void Check(bool valid, const char* message)
{
	if (!valid) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
	Trail npc, player;
	Geometry geometry;
	Material material{ { 0.025f, 0.080f, 0.650f, 0.37f }, 1.f };
	npc.Update(&geometry, &material, true);
	for (int attack = 0; attack < 1000; ++attack) {
		material.baseColorScale *= 0.1f;
		npc.Update(&geometry, &material);
		Check(std::abs(material.baseColorScale * 0.65f - 14.04f) < 0.0001f, "NPC brightness accumulated across attacks");
		Check(material.baseColor.alpha == 0.37f, "Live brightness reset native drain alpha");
	}
	player.redsandConfig.swordBrightness = 2.f;
	Material playerMaterial{ { 0.8f, 0.03f, 0.1f, 1.f }, 0.002f };
	player.baseColorOverride = playerMaterial.baseColor;
	player.Update(&geometry, &playerMaterial, true);
	Check(std::abs(playerMaterial.baseColorScale * 0.8f - 28.08f) < 0.0001f, "Player brightness did not follow its current settings");
	Check(material.baseColor.alpha == 0.37f, "Another trail changed the NPC alpha");
	auto& custom = player.redsandConfig.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Melee)];
	custom.custom = true; custom.rgb = { .1f, .9f, .2f };
	player.Update(&geometry, &playerMaterial);
	Check(playerMaterial.baseColor.green == .9f && playerMaterial.baseColor.alpha == 1.f, "Live melee RGB did not preserve alpha");
	custom.custom = false;
	player.Update(&geometry, &playerMaterial);
	Check(playerMaterial.baseColor.red == .8f && playerMaterial.baseColor.green == .03f, "Turning RGB off did not restore inherited enchantment color");
	npc.redsandConfig.swordBrightness = 0.f;
	npc.Update(&geometry, &material);
	Check(material.baseColorScale == 0.f, "Zero brightness did not hide particles");
	npc.redsandConfig.swordBrightness = 1.f;
	npc.Update(&geometry, &material);
	Check(material.baseColorScale > 20.f, "Brightness did not recover after zero");
	geometry.name = "OtherModParticles";
	material.baseColorScale = 3.f;
	npc.Update(&geometry, &material);
	Check(material.baseColorScale == 3.f, "Unrelated effect brightness changed");
	geometry.name = "RedsandParticles_Drain_v1";
	npc.redsandLegacyTrail = true;
	npc.Update(&geometry, &material);
	Check(material.baseColorScale == 3.f, "Filtered Precision ribbon brightness changed");


	Trail depleted;
	Material white{ {1.f, 1.f, 1.f, .27f}, 4.f };
	depleted.Update(&geometry, &white, true);
	Check(white.baseColor.red == .025f && white.baseColor.green == .080f && white.baseColor.blue == .650f,
		"Depleted weapon inherited stale white material instead of resolved blue");
	Check(white.baseColor.alpha == .27f, "Depleted weapon color reset fade alpha");
}
