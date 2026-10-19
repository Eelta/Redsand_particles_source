#pragma once
#include <array>
#include <optional>

namespace RedsandMaterialColor
{



	template <class Shader, class Material, class Release>
	bool Commit(Shader* shader, Material*& material, const std::array<float, 3>& rgb,
		std::optional<float> glow, bool force, Release release)
	{
		if (!shader || !material) { return false; }
		if (!force && material->baseColor.red == rgb[0] && material->baseColor.green == rgb[1] &&
			material->baseColor.blue == rgb[2] && (!glow || material->baseColorScale == *glow)) { return true; }
		auto* copy = static_cast<Material*>(material->Create());
		if (!copy) { return false; }
		copy->CopyMembers(material);
		copy->baseColor.red = rgb[0]; copy->baseColor.green = rgb[1]; copy->baseColor.blue = rgb[2];
		if (glow) { copy->baseColorScale = *glow; }
		shader->SetMaterial(copy, true);
		release(copy);
		material = static_cast<Material*>(shader->GetMaterial());
		return material != nullptr;
	}
}
