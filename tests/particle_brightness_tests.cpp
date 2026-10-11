#include "../source/RedsandSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace std::literals;

struct Color { float red, green, blue, alpha; };
struct Material { Color baseColor; float baseColorScale; };
struct Geometry { std::string_view name = "RedsandParticles_Drain_v1"; };

struct Trail
{
	bool redsandLegacyTrail = false;
	RedsandSettings::Config redsandConfig;
	void LogRedsandParticles(Geometry*, std::string_view) {}
	void Update(Geometry* a_geometry, Material* effectShaderMaterial, bool a_init = false)
	{
		const bool redsandMaterialCopied = true;
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
	for (int attack = 0; attack < 1000; ++attack) {
		material.baseColorScale *= 0.1f;
		npc.Update(&geometry, &material);
		Check(std::abs(material.baseColorScale * 0.65f - 14.04f) < 0.0001f, "NPC brightness accumulated across attacks");
		Check(material.baseColor.alpha == 0.37f, "Live brightness reset native drain alpha");
	}
	player.redsandConfig.swordBrightness = 2.f;
	Material playerMaterial{ { 0.8f, 0.03f, 0.1f, 1.f }, 0.002f };
	player.Update(&geometry, &playerMaterial);
	Check(std::abs(playerMaterial.baseColorScale * 0.8f - 28.08f) < 0.0001f, "Player brightness did not follow its current settings");
	Check(material.baseColor.alpha == 0.37f, "Another trail changed the NPC alpha");
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
}
